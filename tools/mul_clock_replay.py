#!/usr/bin/env python3
"""Read-only ALR REF_RX text or explicit timestamp JSONL -> production C clocks.

Raw timestamp differences define each domain. In text logs only, coarse liveness
is reconstructed from reference deltas; no host reception time is used as UWB time.
Quadratic forecasts are an offline diagnostic, never applied to firmware.
"""
import argparse
import ctypes
import json
import math
from pathlib import Path
import re
import statistics
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
MS = 63897600
MASK = (1 << 40)-1


def delta(a, b):
    return ((a-b+(1 << 39)) & MASK)-(1 << 39)


def observations(path, period):
    source, local, reset = 132, 133, True
    previous, uptime = None, 0
    clock_epoch = None
    pattern = re.compile(r'REF_RX src=0x([0-9a-f]+) seq=(\d+) tx=0x([0-9a-f]+) rx=0x([0-9a-f]+)', re.I)
    for line in path.read_text(errors='replace').splitlines():
        if 'Mode initialized' in line:
            match = re.search(r'src=0x([0-9a-f]+)', line, re.I)
            if match:
                local = int(match[1], 16)
            previous, reset = None, True
        if line.startswith('{'):
            row = json.loads(line)
            if row.get('type') == 'clock':
                marker = (row['node'], row.get('epoch'), row.get('mapping_epoch'))
                reset_clock = marker != clock_epoch
                clock_epoch = marker
                row = dict(row, reset=reset_clock, sequence=row['reference_sequence'], uptime_ms=row.get('uptime_ms', row.get('u_ms')),
                           reference=row['reference_node'], local=row['node'])
            if all(key in row for key in ('sequence', 'reference_tx_dtu', 'local_rx_dtu', 'uptime_ms')):
                yield row
            continue
        match = pattern.search(line)
        if not match:
            continue
        source, sequence, tx, rx = (int(value, base) for value, base in zip(match.groups(), (16, 10, 16, 16)))
        dt = period if previous is None else max(1, round(delta(tx, previous)/MS))
        if previous is not None and delta(tx, previous) <= 0:
            reset = True
        uptime += dt
        yield dict(sequence=sequence, reference_tx_dtu=tx, local_rx_dtu=rx,
                   uptime_ms=uptime & 0xffffffff, reference=source, local=local, reset=reset)
        previous, reset = tx, False


def polynomial_predict(points, x, degree):
    """Small centered least-squares solve; diagnostic float is host-only."""
    origin = points[-1][0]
    center = statistics.mean(y for _, y in points)
    values = [(a-origin, b-center) for a, b in points]
    n = degree+1
    a = [[sum(t**(i+j) for t, _ in values) for j in range(n)] +
         [sum(y*t**i for t, y in values)] for i in range(n)]
    for i in range(n):
        pivot = max(range(i, n), key=lambda j: abs(a[j][i]))
        a[i], a[pivot] = a[pivot], a[i]
        if abs(a[i][i]) < 1e-20:
            return None
        divisor = a[i][i]
        a[i] = [v/divisor for v in a[i]]
        for j in range(n):
            if j != i:
                factor = a[j][i]
                a[j] = [u-factor*v for u, v in zip(a[j], a[i])]
    return center+sum(a[i][-1]*(x-origin)**i for i in range(n))


class Replay:
    def __init__(self, library, window=24, period=1000):
        self.lib, self.window, self.period = library, window, period
        self.context = None
        self.identity = None
        self.active_period = None

    def feed(self, row):
        for name, maximum in (('sequence', 0xffffffff), ('reference_tx_dtu', MASK), ('local_rx_dtu', MASK), ('uptime_ms', 0xffffffff)):
            value = row[name]
            if type(value) is not int or not 0 <= value <= maximum:
                raise ValueError(f'invalid {name}')
        if not -1 <= row.get('one_way_dtu', -1) <= 63898 or not 0 <= row.get('delay_age_ms', 0) <= 0xffffffff:
            raise ValueError('invalid delay sample')
        period = row.get('sample_period_ms')
        if period is None:
            period = self.period
        if type(period) is not int or not 10 <= period <= 2000:
            raise ValueError('invalid sample_period_ms')
        age = row.get('capture_age_ms')
        if age is not None and (type(age) is not int or not 0 <= age <= 65535):
            raise ValueError('invalid capture_age_ms')
        # Old captures did not carry RX age: preserve replay but mark its qualification.
        capture_valid = age is None or age <= 15
        identity = (row.get('reference', 132), row.get('local', 133))
        if self.context is None or row.get('reset') or identity != self.identity or period != self.active_period:
            self.close()
            self.context = self.lib.replay_new(self.window, period, *identity)
            if not self.context:
                raise ValueError('invalid identity or clock configuration')
            self.identity = identity
            self.active_period = period
        output = ctypes.create_string_buffer(1024)
        count = self.lib.replay_step(self.context, row['sequence'], row['reference_tx_dtu'], row['local_rx_dtu'],
                                     row['uptime_ms'], int(capture_valid), row.get('one_way_dtu', -1), row.get('delay_age_ms', 0), output, len(output))
        if not 0 < count < len(output):
            raise ValueError('replay output overflow')
        return dict(json.loads(output.value), sample_period_ms=period, capture_valid=capture_valid if age is not None else None,
                    capture_validity='unknown; assumed for legacy replay' if age is None else 'valid' if capture_valid else 'invalid')

    def close(self):
        if self.context:
            self.lib.replay_free(self.context)
            self.context = None


def build_library(directory):
    output = Path(directory)/'clock.so'
    core = ROOT/'portable/mul_core'
    subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror', '-shared', '-fPIC', '-O2', '-I'+str(core),
                    str(ROOT/'tools/test/clock_fit_replay.c'), str(core/'mul_clock_fit.c'),
                    str(core/'mul_clock.c'), str(core/'mul_common.c'), '-o', str(output)], check=True)
    library = ctypes.CDLL(str(output))
    library.replay_new.argtypes = [ctypes.c_uint]*4
    library.replay_new.restype = ctypes.c_void_p
    library.replay_free.argtypes = [ctypes.c_void_p]
    library.replay_step.argtypes = [ctypes.c_void_p, ctypes.c_uint, ctypes.c_ulonglong, ctypes.c_ulonglong,
                                   ctypes.c_uint, ctypes.c_uint, ctypes.c_int, ctypes.c_int, ctypes.c_void_p, ctypes.c_uint]
    return library


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('logs', nargs='+', type=Path)
    parser.add_argument('--window', type=int, default=24)
    parser.add_argument('--period-ms', type=int, default=1000)
    parser.add_argument('--summary', action='store_true')
    args = parser.parse_args()
    if not 8 <= args.window <= 32 or not 10 <= args.period_ms <= 2000:
        parser.error('window8..32; period-ms10..2000')
    with tempfile.TemporaryDirectory() as work:
        library = build_library(work)
        for path in args.logs:
            replay = Replay(library, args.window, args.period_ms)
            records, points, errors = [], [], [[], []]
            last = None
            x = phase = 0
            try:
                for row in observations(path, args.period_ms):
                    if row.get('reset') or (last and row.get('sample_period_ms', args.period_ms) != last.get('sample_period_ms', args.period_ms)) or row.get('capture_age_ms', 0) is not None and row.get('capture_age_ms', 0) > 15:
                        points, last, x, phase = [], None, 0, 0
                    if last:
                        dt = delta(row['reference_tx_dtu'], last['reference_tx_dtu'])
                        dr = delta(row['local_rx_dtu'], last['local_rx_dtu'])
                        if 0 < dt < 3000*MS:
                            x += dt/(1000*MS)
                            phase += dr-dt
                            if len(points) >= args.window:
                                for degree in (1, 2):
                                    predicted = polynomial_predict(points[-args.window:], x, degree)
                                    if predicted is not None:
                                        errors[degree-1].append(phase-predicted)
                            points.append((x, phase))
                        else:
                            points, x, phase = [(0, 0)], 0, 0
                    else:
                        points.append((0, 0))
                    last = row if row.get('capture_age_ms') is None or row['capture_age_ms'] <= 15 else None
                    result = replay.feed(row)
                    records.append(result)
                    if not args.summary:
                        print(json.dumps(dict(log=str(path), **row, **result), allow_nan=False))
                if args.summary:
                    ready = [r for r in records if r['fit_ready']]
                    rms = lambda values: math.sqrt(statistics.mean(v*v for v in values)) if values else None
                    print(json.dumps(dict(log=str(path), observations=len(records), ready=len(ready),
                          fitted_ppb=statistics.mean(r['fitted_ppb'] for r in ready) if ready else None,
                          applied=sum(r['fit_applied'] for r in ready), changing_rate_fallback=sum(r['fallback'] == 2 for r in ready),
                          median_ppb=statistics.mean(r['median_ppb'] for r in ready) if ready else None,
                          regression_ppb=statistics.mean(r['regression_ppb'] for r in ready) if ready else None,
                          last=records[-1] if records else None,
                          linear_forecast_rms_dtu=rms(errors[0]), quadratic_forecast_rms_dtu=rms(errors[1]),
                          interpretation='apparent arrival rate; propagation and device biases remain confounded'), allow_nan=False))
            finally:
                replay.close()


if __name__ == '__main__':
    main()
