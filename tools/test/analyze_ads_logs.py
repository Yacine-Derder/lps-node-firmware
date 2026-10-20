#!/usr/bin/env python3
"""Deterministic ADS log audit. Standard library only; never infer cross-file PDR.

TIMES/RESULT association is unique within a file/epoch/session/transaction.
Counter snapshots are cumulative, not individual events. See --help for filters.
"""
import argparse
import csv
from collections import Counter
from dataclasses import dataclass, field
from fractions import Fraction
import hashlib
import math
from pathlib import Path
import re
import statistics

MASK = (1 << 40) - 1
MAX_INTERVAL = 6389760000
METRES = Fraction(299792458, 63897600000)
COUNTERS = ('attempt', 'done', 'abort', 'reject', 'invalid', 'err', 'to')
START = re.compile(r'MULH (?:INIT|STAT|TIMES|RESULT|ABORT|TIME)\b|\[(?:EPUCK|MOTOR|SAFETY|STREAM|CONSOLE|ESP|LINK)\]')
FIELDS = re.compile(r'(?<!\S)([A-Za-z0-9_]+)=([^\s]+)')


def difference(a, b):
    return ((a - b + (1 << 39)) & MASK) - (1 << 39)


def recompute(t):
    """Exact rational independent implementation, with the firmware's bounds."""
    if len(t) != 6 or any(v < 0 or v > MASK for v in t):
        raise ValueError('timestamp_width')
    ra, rb, da, db = (difference(t[3], t[0]), difference(t[5], t[2]),
                      difference(t[4], t[3]), difference(t[2], t[1]))
    if any(v <= 0 or v > MAX_INTERVAL for v in (ra, rb, da, db)):
        raise ValueError('timestamp_order_or_interval')
    ratio = Fraction(rb + db, ra + da)
    if not Fraction(999, 1000) <= ratio <= Fraction(1001, 1000):
        raise ValueError('clock_envelope')
    tof = Fraction(ra * rb - da * db, ra + rb + da + db)
    return (ra, rb, da, db), tof, ratio, tof * METRES


def percentile(values, percent):
    ordered = sorted(values)
    pos = (len(ordered) - 1) * percent / 100
    low = int(pos)
    return ordered[low] + (ordered[min(low + 1, len(ordered)-1)] - ordered[low]) * (pos-low)


def summary(values, reference=None):
    if not values:
        return {'n': 0}
    result = dict(n=len(values), mean=statistics.mean(values), median=statistics.median(values),
                  sd_population=statistics.pstdev(values), minimum=min(values), maximum=max(values))
    result.update({f'p{p}': percentile(values, p) for p in (1, 5, 25, 50, 75, 95, 99)})
    if reference is not None:
        result['bias_vs_approx_reference'] = result['mean']-reference
        result['rmse_vs_approx_reference'] = math.sqrt(statistics.mean((v-reference)**2 for v in values))
    return result


@dataclass
class Epoch:
    number: int
    origin: str
    session: str = 'unknown'
    snapshots: list = field(default_factory=list)
    times: list = field(default_factory=list)
    results: list = field(default_factory=list)
    events: list = field(default_factory=list)
    notes: list = field(default_factory=list)
    timing: list = field(default_factory=list)

    def has_data(self):
        return bool(self.snapshots or self.times or self.results)


@dataclass
class Capture:
    name: str
    role: str
    sha256: str
    epochs: list = field(default_factory=list)
    malformed: list = field(default_factory=list)
    concatenated: list = field(default_factory=list)


def session(fields):
    value = fields.get('session')
    if value is None:
        return None
    if not re.fullmatch(r'[0-9A-Fa-f]{16}', value) or int(value, 16) == 0:
        raise ValueError('malformed_session')
    return value.upper()


def parse(path, role):
    raw = Path(path).read_bytes()
    capture = Capture(Path(path).name, role, hashlib.sha256(raw).hexdigest())
    current = Epoch(1, 'partial_capture')
    capture.epochs.append(current)
    previous_snapshot = None

    def new_epoch(origin, sid):
        nonlocal current
        current = Epoch(len(capture.epochs)+1, origin, sid or 'unknown')
        capture.epochs.append(current)

    for line_number, line in enumerate(raw.decode('utf-8', errors='replace').splitlines(), 1):
        line = re.sub(r'\x1b\[[0-?]*[ -/]*[@-~]', '', line)
        starts = list(START.finditer(line))
        if len(starts) > 1:
            capture.concatenated.append(line_number)
        parts = [line[m.start():starts[i+1].start() if i+1<len(starts) else len(line)].strip()
                 for i, m in enumerate(starts)] or [line.strip()]
        for part_number,text in enumerate(parts):
            if not text:
                continue
            pairs=FIELDS.findall(text)
            fields = dict(pairs)
            kind = text.split()[1] if text.startswith('MULH ') and len(text.split()) > 1 else 'event'
            try:
                if len(pairs)!=len(fields):
                    raise ValueError('duplicate_fields')
                sid = session(fields) if kind in ('INIT', 'STAT', 'TIMES', 'RESULT', 'ABORT') else None
                if kind == 'INIT':
                    if 'FAILED' in text:
                        current.events.append((line_number, text))
                        continue
                    if fields.get('role') not in ('1', '2'):
                        raise ValueError('malformed_init')
                    capture.role = 'initiator' if fields['role']=='1' else 'responder'
                    # Responder INIT nonce is a placeholder, not its peer's session.
                    sid = sid if capture.role=='initiator' else None
                    if current.has_data():
                        new_epoch('init_or_restart', sid)
                    else:
                        current.origin, current.session = 'init', sid or 'unknown'
                    previous_snapshot = None
                    current.events.append((line_number, text))
                    continue
                if kind == 'STAT':
                    if any(not re.fullmatch(r'[0-9]+', fields.get(k, '')) for k in (*COUNTERS, 'stage')):
                        raise ValueError('incomplete_stat')
                    snap = {k: int(fields[k]) for k in (*COUNTERS, 'stage')}
                    if any(snap[k] > 0xffffffff for k in COUNTERS) or not 0 <= snap['stage'] <= 7:
                        raise ValueError('invalid_stat_range')
                    if capture.role == 'responder' and snap['attempt'] == 0:
                        sid = None
                    # Counter wrap is distinguished from a smaller non-wrap restart.
                    if previous_snapshot and any(snap[k] < previous_snapshot[k] and
                        (snap[k]-previous_snapshot[k]) % (1<<32) >= (1<<31) for k in COUNTERS):
                        new_epoch('counter_reset', sid)
                    if previous_snapshot and any(snap[k] < previous_snapshot[k] for k in COUNTERS):
                        current.notes.append(f'line{line_number}: modulo32 counter wrap; endpoint absolute ratios are modulo counts')
                    previous_snapshot = snap
                if sid and sid != current.session:
                    if current.has_data():
                        new_epoch('session_change', sid)
                    else:
                        current.session = sid
                if kind == 'STAT':
                    if 'bad_range' in fields:
                        if not re.fullmatch(r'[0-9]+',fields['bad_range']) or int(fields['bad_range'])>0xffffffff:
                            raise ValueError('invalid_range_counter')
                        snap['bad_range']=int(fields['bad_range'])
                    snap['line'] = line_number
                    current.snapshots.append(snap)
                    pending = int(snap['stage'] != 0)
                    if (snap['done']+snap['abort']+pending) % (1<<32) != snap['attempt']:
                        current.notes.append(f'line{line_number}: attempt != done+abort+active (mod 2^32)')
                elif kind == 'TIME':
                    keys=('hz','spi_off_cycles','service_cycles','gap_cycles','calls')
                    if any(not re.fullmatch(r'[0-9]+', fields.get(k,'')) for k in keys) or int(fields['hz'])<=0:
                        raise ValueError('malformed_timing')
                    current.timing.append({k:int(fields[k]) for k in keys})
                elif kind in ('TIMES', 'RESULT'):
                    if not re.fullmatch(r'[0-9]+', fields.get('id', '')) or not 1 <= int(fields['id']) <= 0xffffffff:
                        raise ValueError('invalid_id')
                    row = dict(fields, line=line_number, position=(line_number,part_number), id=int(fields['id']), session=current.session)
                    (current.times if kind=='TIMES' else current.results).append(row)
                else:
                    if re.search(r'FAULT|watchdog|SAFETY|STOP|ARM|STREAM|dropped|DISCONNECT|INIT FAILED|MULH ABORT', text, re.I):
                        current.events.append((line_number, text))
            except ValueError as error:
                capture.malformed.append((line_number, str(error), text))
    return capture


def audit_times(row, t):
    for i in range(1,7):
        row[f't{i}']=t.get(f't{i}','')
    if any(not re.fullmatch(r'[0-9a-fA-F]{10}', str(row[f't{i}'])) for i in range(1,7)):
        raise ValueError('timestamp_width')
    stamps=[int(row[f't{i}'],16) for i in range(1,7)]
    row.update(zip(('Ra','Rb','Da','Db'),(difference(stamps[3],stamps[0]),difference(stamps[5],stamps[2]),
                                       difference(stamps[4],stamps[3]),difference(stamps[2],stamps[1]))))
    row['wrap_intervals']=sum(stamps[a]<stamps[b] for a,b in ((3,0),(5,2),(4,3),(2,1)))
    _,tof,ratio,metres=recompute(stamps)
    row.update(recomputed_tof_dtu=float(tof),recomputed_ratio=float(ratio),recomputed_nominal_m=float(metres))
    return stamps,tof,ratio,metres


def analyse(capture, reference=1.0, max_range=10.0, jump=0.5):
    samples = []
    integrity = {'missing_times', 'ambiguous_association', 'malformed_result', 'nonfinite',
                 'negative', 'outside_lab_bound', 'firmware_invalid', 'timestamp_width',
                 'timestamp_order_or_interval', 'clock_envelope', 'tof_mismatch',
                 'metres_mismatch', 'ratio_mismatch', 'firmware_validity_mismatch', 'out_of_order_result'}
    for epoch in capture.epochs:
        rows = []
        last_id, last_range = None, None
        prior_stamps = None
        times = {}
        for t in epoch.times:
            times.setdefault(t['id'], []).append(t)
        multiplicity = {}
        for result in epoch.results:
            multiplicity[result['id']] = multiplicity.get(result['id'], 0)+1
        for result in epoch.results:
            row = dict(record_kind='RESULT', file=capture.name, epoch=epoch.number, session=epoch.session,
                       id=result['id'], line_result=result['line'], flags=set(),
                       tof_dtu=result.get('tof_dtu', ''), ratio=result.get('ratio', ''),
                       nominal_m=result.get('nominal_m', ''), range_valid_fw=result.get('range_valid', ''))
            flags = row['flags']
            try:
                value, tof, ratio = (float(result[k]) for k in ('nominal_m', 'tof_dtu', 'ratio'))
                if not all(math.isfinite(v) for v in (value, tof, ratio)):
                    flags.add('nonfinite')
                else:
                    row['value'], row['ratio_value'] = value, ratio
                    if value < 0 or tof < 0:
                        flags.add('negative')
                    if value > max_range:
                        flags.add('outside_lab_bound')
                    if not .999 <= ratio <= 1.001:
                        flags.add('clock_envelope')
                    if last_range is not None and abs(value-last_range) > jump:
                        flags.add('large_jump')  # Annotation, not automatic exclusion of the next good sample.
                    last_range = value
                if result.get('range_valid') == '0':
                    flags.add('firmware_invalid')
                if 'range_valid' in result and result['range_valid'] not in ('0', '1'):
                    flags.add('malformed_result')
            except (ValueError, KeyError):
                flags.add('malformed_result')
            if last_id is not None:
                advance = (result['id']-last_id) % (1<<32)
                if result['id'] < last_id or advance >= (1<<31):
                    flags.add('out_of_order_result')
                elif advance > 1:
                    row['missing_result_ids_before'] = advance-1
            last_id = result['id']
            candidates = times.get(result['id'], [])
            if len(candidates) != 1 or multiplicity[result['id']] != 1:
                flags.add('missing_times' if not candidates else 'ambiguous_association')
            else:
                t = candidates[0]
                row['line_times'] = t['line']
                if t['position'] >= result['position']:
                    flags.add('ambiguous_association')
                try:
                    stamps,exact_tof,exact_ratio,metres=audit_times(row,t)
                    if exact_tof<0:
                        flags.add('negative')
                        if result.get('range_valid')=='1':
                            flags.add('firmware_validity_mismatch')
                    if prior_stamps is not None:
                        row['cross_result_clock_decreases'] = int(stamps[0]<prior_stamps[0])+int(stamps[1]<prior_stamps[1])
                    prior_stamps = stamps
                    if 'value' in row:
                        row['tof_error_dtu'] = float(exact_tof)-float(result['tof_dtu'])
                        row['metres_error'] = float(metres)-row['value']
                        row['ratio_error'] = float(exact_ratio)-row['ratio_value']
                        if abs(row['tof_error_dtu']) > .00001:
                            flags.add('tof_mismatch')
                        if abs(row['metres_error']) > .0000501:
                            flags.add('metres_mismatch')
                        if abs(row['ratio_error']) > .000000000501:
                            flags.add('ratio_mismatch')
                except ValueError as error:
                    flags.add(str(error))
            row['primary_valid'] = not bool(flags & integrity)
            rows.append(row)
        # Preserve timestamp candidates with no RESULT, e.g. a classified math abort.
        # They are not implicitly promoted into completed range statistics.
        for transaction,candidates in times.items():
            if transaction in multiplicity:
                continue
            for candidate in candidates:
                row=dict(record_kind='TIMES_ONLY',file=capture.name,epoch=epoch.number,session=epoch.session,
                         id=transaction,line_times=candidate['line'],flags={'missing_result'},primary_valid=False)
                try:
                    _,tof,_,_=audit_times(row,candidate)
                    if tof<0:row['flags'].add('negative')
                except ValueError as error:
                    row['flags'].add(str(error))
                rows.append(row)
        primary = [r['value'] for r in rows if r['primary_valid']]
        centre = statistics.median(primary) if primary else 0
        mad = statistics.median(abs(v-centre) for v in primary) if primary else 0
        threshold = max(3*1.4826*mad, .05)
        ratios = [r['ratio_value'] for r in rows if r['primary_valid']]
        rcentre = statistics.median(ratios) if ratios else 1
        rmad = statistics.median(abs(v-rcentre) for v in ratios) if ratios else 0
        for row in rows:
            if row['primary_valid'] and abs(row['value']-centre)>threshold:
                row['flags'].add('range_mad_outlier')
            if 'ratio_value' in row and abs(row['ratio_value']-rcentre)>max(6*1.4826*rmad, 1e-6):
                row['flags'].add('ratio_outlier')
            row['robust_valid'] = row['primary_valid'] and 'range_mad_outlier' not in row['flags']
        epoch.analysis = dict(rows=rows, raw=summary([r['value'] for r in rows if 'value' in r], reference),
                              primary=summary(primary,reference),
                              robust=summary([r['value'] for r in rows if r['robust_valid']],reference),
                              ratio=summary([r['ratio_value'] for r in rows if 'ratio_value' in r]),
                              mad=mad, threshold=threshold,
                              unmatched_times=sum(len(v) for k,v in times.items() if k not in multiplicity))
        samples.extend(rows)
    return samples


def number(value):
    return f'{value:.9g}' if isinstance(value, float) else str(value)


def closed_initiator_cohort(capture, epoch):
    """Conservative reconstruction using the documented id==attempt initiator model.

    Only use an INIT-observed epoch, unique ordered complete results, entry POLL
    snapshot, final pending POLL snapshot plus its last RESULT, and exact counter
    conservation. This is not cross-radio delivery or a guess from missing IDs.
    """
    if capture.role!='initiator' or epoch.origin not in ('init','init_or_restart') or not epoch.results or epoch.notes:
        return None
    results=epoch.results
    ids=[r['id'] for r in results]
    if ids!=sorted(set(ids)):
        return None
    entries=[s for s in epoch.snapshots if s['attempt']==ids[0] and s['stage']==1 and s['line']<results[0]['line']]
    if not entries or not epoch.snapshots:
        return None
    first,last=entries[-1],epoch.snapshots[-1]
    tail=[r for r in results if r['line']>last['line']]
    if last['attempt']!=ids[-1] or last['stage']!=1 or len(tail)!=1 or tail[0]['id']!=ids[-1]:
        return None
    completed=last['done']+1-first['done']
    aborted=last['abort']-first['abort']
    attempts=ids[-1]-ids[0]+1
    if completed!=len(results) or aborted<0 or completed+aborted!=attempts:
        return None
    if any(not r.get('line_times') or r['flags'] & {'ambiguous_association','missing_times','missing_result','malformed_result','nonfinite','timestamp_width','timestamp_order_or_interval','clock_envelope','tof_mismatch','metres_mismatch','ratio_mismatch'} for r in epoch.analysis['rows']):
        return None
    return dict(first_id=ids[0],last_id=ids[-1],completed=completed,aborted=aborted,attempts=attempts)


def report(captures, reference=1.0, max_range=10.0, jump=.5):
    out = ['# ADS-TWR capture analysis', '',
           f'Approximate reference: {reference:g} m (not certified ground truth).', '',
           'Selection: raw statistics include every finite parsed RESULT, including negatives. '
           'Primary requires unique preceding TIMES in the same file/epoch/session/id, full40-bit '
           'markers, positive <=100ms modular intervals, ratio within +/-1000ppm, independent '
           'agreement within0.00001DTU/0.0000501m/0.000000000501ratio, nonnegative range/ToF '
           f'and no firmware invalid flag, with range <= {max_range:g}m (explicit laboratory bound). '
           'Ambiguous duplicate IDs and out-of-order results are excluded. '
           'Robust additionally excludes |range-median(primary)| > max(3*1.4826*MAD,0.05m). '
           f'Jumps > {jump:g}m and ratio deviations > max(6*1.4826*MAD,1ppm) are annotations; '
           'they alone do not exclude a following good sample. All RESULT samples/flags and unmatched TIMES candidates remain in CSV; TIMES_ONLY candidates are excluded from range statistics.', '',
           'SD is population SD. Percentiles use linear interpolation at (n-1)*p. '
           'RMSE/bias use the approximate reference. No counter or measurement is pooled across files/epochs. '
           'Unmatched counter windows cannot establish packet delivery or UART/range causation.', '']
    for capture in captures:
        out += [f'## {capture.name} ({capture.role})', '', f'SHA-256: `{capture.sha256}`.', '',
                f'Malformed/truncated records: {len(capture.malformed)}; concatenated lines: {capture.concatenated or "none"}.']
        for line, reason, _ in capture.malformed:
            out.append(f'- Line{line}: {reason}.')
        for epoch in capture.epochs:
            a = epoch.analysis
            out += ['', f'### Epoch {epoch.number}, session {epoch.session}, origin {epoch.origin}', '',
                    f'Printed RESULTS={len(epoch.results)}, TIMES={len(epoch.times)}, '
                    f'TIMES without RESULT={a["unmatched_times"]}.']
            if epoch.snapshots:
                first,last=epoch.snapshots[0],epoch.snapshots[-1]
                out += ['', '| Counter | First snapshot | Last snapshot | Modulo32 delta |', '|---|---:|---:|---:|']
                for k in COUNTERS + (('bad_range',) if 'bad_range' in first and 'bad_range' in last else ()):
                    out.append(f'| {k} | {first[k]} | {last[k]} | {(last[k]-first[k])%(1<<32)} |')
                resolved=last['done']+last['abort']
                wrapped=any(last[k]<first[k] for k in COUNTERS)
                if resolved and not wrapped:
                    out += ['', f'Last cumulative snapshot: completed/resolved={last["done"]}/{resolved} '
                            f'({100*last["done"]/resolved:.6f}%); aborted/resolved={last["abort"]}/{resolved} '
                            f'({100*last["abort"]/resolved:.6f}%). These are endpoint lifetime ratios, '
                            'not cross-file delivery rates or necessarily capture-only rates.']
                delta={k:(last[k]-first[k])%(1<<32) for k in COUNTERS}
                if delta['attempt']:
                    out += [f'Within first/last snapshot lines{first["line"]}–{last["line"]}: '
                            f'done/attempt flow={delta["done"]}/{delta["attempt"]} '
                            f'({100*delta["done"]/delta["attempt"]:.6f}%), '
                            f'abort/attempt flow={delta["abort"]}/{delta["attempt"]} '
                            f'({100*delta["abort"]/delta["attempt"]:.6f}%). '
                            f'Endpoint stages={first["stage"]}/{last["stage"]}; '
                            + ('both idle, so no boundary in-flight exchange.' if first['stage']==last['stage']==0
                               else 'in-flight boundary exchanges can differ; this is not a closed transaction cohort.')]
                # Preserve any unreflected last RESULT instead of silently losing it.
                tail=[r for r in epoch.results if r['line']>last['line']]
                if tail:
                    out.append(f'{len(tail)} RESULT record(s) follow the last snapshot (IDs: '
                               + ','.join(str(r['id']) for r in tail)+'); last done counter is therefore a lower bound at capture end.')
            cohort=closed_initiator_cohort(capture,epoch)
            if cohort:
                out += ['', f'Model-checked closed initiator cohort IDs{cohort["first_id"]}–{cohort["last_id"]}: '
                        f'completed={cohort["completed"]}/{cohort["attempts"]} ({100*cohort["completed"]/cohort["attempts"]:.6f}%), '
                        f'aborted={cohort["aborted"]}/{cohort["attempts"]} ({100*cohort["aborted"]/cohort["attempts"]:.6f}%). '
                        'Uses INIT, id==attempt, entry pending-POLL counters, final RESULT after pending-POLL snapshot, '
                        'unique paired records and exact completed+aborted conservation. Completed includes negative raw ToF. '
                        'This is one initiator transaction cohort, not a cross-radio PDR.']
            if epoch.notes:
                out += ['', *[f'- {n}' for n in epoch.notes]]
            if epoch.results:
                out += ['', '| Statistic | All finite raw | Primary | Robust |', '|---|---:|---:|---:|']
                for k in ('n','mean','median','sd_population','bias_vs_approx_reference','rmse_vs_approx_reference',
                          'minimum','p1','p5','p25','p50','p75','p95','p99','maximum'):
                    out.append('| '+k+' | '+' | '.join(number(a[group].get(k,'NA')) for group in ('raw','primary','robust'))+' |')
                out += ['', f'Primary MAD={a["mad"]:.9g}m; robust exclusion threshold={a["threshold"]:.9g}m.',
                        'Clock ratio (all finite parsed): '+', '.join(f'{k}={number(v)}' for k,v in a['ratio'].items())+'.',
                        f'Rows with modular timestamp-wrap intervals: {sum(bool(r.get("wrap_intervals")) for r in a["rows"])}.',
                        f'Across-result local t1/t2 decreases: {sum(r.get("cross_result_clock_decreases",0) for r in a["rows"])} '
                        '(consistent with timestamp wraps or resets, not proof of either alone).',
                        'Maximum independent absolute residual: '+', '.join(
                            f'{k}={max((abs(r[k]) for r in a["rows"] if k in r),default=0):.9g}'
                            for k in ('tof_error_dtu','metres_error','ratio_error'))+'.',
                        f'Independent math/unit/ratio mismatches: {sum(bool(r["flags"] & {"tof_mismatch","metres_mismatch","ratio_mismatch"}) for r in a["rows"])}.',
                        'Missing RESULT IDs between printed results: '
                        f'{sum(r.get("missing_result_ids_before",0) for r in a["rows"])} '
                        '(aborts or logging omissions; not RF loss).', '']
                anomalies=[r for r in a['rows'] if r['flags']]
                out += ['| ID | Range m | Flags |', '|---|---:|---|']
                for r in anomalies:
                    out.append(f'| {r["id"]} | {r.get("nominal_m","NA")} | {", ".join(sorted(r["flags"]))} |')
                if not anomalies:
                    out.append('| — | — | none |')
            if epoch.timing:
                out += ['', 'Timing maxima (microseconds; sampled outer SPI mask/service/start gap, not WCET): '+
                        ', '.join(f'{k}={max(t[k]/t["hz"]*1e6 for t in epoch.timing):.9g}'
                                  for k in ('spi_off_cycles','service_cycles','gap_cycles'))+'.']
            if epoch.events:
                fault_rows=[(n,t) for n,t in epoch.events if '[EPUCK] FAULT' in t]
                stop_acks=sum('[MOTOR] OK STOP' in t for _,t in epoch.events)
                watchdog=sum(bool(re.search('watchdog|heartbeat.*(lost|timeout)|control_hb_timeout',t,re.I)) for _,t in epoch.events)
                aborts=[dict(FIELDS.findall(t)) for _,t in epoch.events if t.startswith('MULH ABORT')]
                causes=Counter(a.get('cause','unknown') for a in aborts)
                stages=Counter(a.get('stage','unknown') for a in aborts)
                if aborts:
                    out += ['', f'Visible classified abort records={len(aborts)}; causes={dict(sorted(causes.items()))}; stages={dict(sorted(stages.items()))}. '
                            f'Last cumulative by_stage={aborts[-1].get("by_stage","unknown")} (stage counts saturate at65535, never sum successive vectors).']
                drop_reports=[int(m[1]) for _,t in epoch.events if (m:=re.search(r'\[CONSOLE\] ([0-9]+) messages dropped',t))]
                out += ['', f'Visible UART fault reports={len(fault_rows)}; STOP acknowledgements={stop_acks}; '
                        f'watchdog/heartbeat-loss reports={watchdog}; console drop reports={len(drop_reports)} '
                        f'(reported interval message drops={sum(drop_reports)}). Missing reports do not establish zero faults.']
                out += ['', 'Recorded events (cumulative fault counters are not independent events):', '']
                for line, text in epoch.events:
                    out.append(f'- Line{line}: `{text.replace(chr(96), chr(39))}`')
    out += ['', 'Radio error counts include idle conditions; frame-wait timeouts are expected between1Hz exchanges. '
            'Old firmware has no abort-cause counters, so exact failed stages cannot be reconstructed. '
            'Responder done means REPORT TX; initiator done means computed exchange, including negative raw ToF in legacy firmware. '
            'Legacy TIMES has no session tag; unknown-session records remain explicitly unknown, and no cross-file association is attempted.', '']
    return '\n'.join(out)


CSV_FIELDS = ['record_kind','file','epoch','session','id','line_result','line_times','tof_dtu','ratio','nominal_m',
              'range_valid_fw',*[f't{i}' for i in range(1,7)],'Ra','Rb','Da','Db','recomputed_tof_dtu',
              'recomputed_ratio','recomputed_nominal_m','wrap_intervals','missing_result_ids_before',
              'tof_error_dtu','metres_error','ratio_error','cross_result_clock_decreases',
              'flags','primary_valid','robust_valid']


def main(argv=None):
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--initiator', type=Path, required=True)
    p.add_argument('--responder', type=Path)
    p.add_argument('--reference-m',type=float,default=1.0,help='approximate experimental reference, not certified ground truth')
    p.add_argument('--max-range-m',type=float,default=10.0,help='explicit laboratory plausibility bound; raw samples retained')
    p.add_argument('--jump-m',type=float,default=.5,help='annotation only')
    p.add_argument('--csv',type=Path,required=True)
    p.add_argument('--report',type=Path,required=True)
    a=p.parse_args(argv)
    if any(not math.isfinite(v) or v<=0 for v in (a.reference_m,a.max_range_m,a.jump_m)):
        p.error('numeric options must be finite and positive')
    inputs=[a.initiator]+([a.responder] if a.responder else [])
    if any(not q.is_file() for q in inputs):
        p.error('input log not found; place the complete raw logs locally or supply their actual paths')
    if any(out.resolve() in [q.resolve() for q in inputs] for out in (a.csv,a.report)) or a.csv.resolve()==a.report.resolve():
        p.error('outputs must differ from each other and all input logs')
    captures=[parse(a.initiator,'initiator')]+([parse(a.responder,'responder')] if a.responder else [])
    rows=[]
    for c in captures:
        rows.extend(analyse(c,a.reference_m,a.max_range_m,a.jump_m))
    with a.csv.open('w',newline='') as f:
        writer=csv.DictWriter(f,CSV_FIELDS,extrasaction='ignore',lineterminator='\n');writer.writeheader()
        for row in rows:
            writer.writerow(dict(row,flags=';'.join(sorted(row['flags']))))
    a.report.write_text(report(captures,a.reference_m,a.max_range_m,a.jump_m))
    print(f'Wrote {len(rows)} raw RESULT/candidate rows to {a.csv} and report to {a.report}')


if __name__=='__main__':
    main()
