#!/usr/bin/env python3
"""Propose an additive correction from audited raw ADS logs. Never fit a scale.

No hardware access, no SAVE, no automatic firmware configuration. Each run has
one vote (median bias); sample count cannot overwhelm reference uncertainty.
"""
import argparse
from collections import Counter
import csv
import json
import math
from pathlib import Path
import statistics
from analyze_ads_logs import analyse, parse, percentile, CSV_FIELDS


def finite_positive(value):
    number = float(value)
    if not math.isfinite(number) or number <= 0:
        raise argparse.ArgumentTypeError('must be finite and positive')
    return number


def run_spec(text):
    try:
        reference, name = text.split('=', 1)
        reference = finite_positive(reference)
        if not name:
            raise ValueError('empty path')
        return reference, Path(name)
    except (ValueError, argparse.ArgumentTypeError) as error:
        raise argparse.ArgumentTypeError('use REFERENCE_METRES=LOG_PATH') from error


def fit(runs, uncertainty, max_range):
    rows, results = [], []
    seen = set()
    for reference, path in runs:
        if path.resolve() in seen:
            raise ValueError('each input log must be unique; duplicate files are not independent runs')
        seen.add(path.resolve())
        capture = parse(path, 'initiator')
        audited = analyse(capture, reference=reference, max_range=max_range)
        for row in audited:
            row['reference_m'] = reference
            row['reference_uncertainty_m'] = uncertainty
        rows.extend(audited)
        accepted = [r['value'] for r in audited if r['record_kind'] == 'RESULT' and r['primary_valid']]
        if not accepted:
            raise ValueError(f'{path.name}: no timestamp-audited valid raw ranges; cannot fit calibration')
        centre = statistics.median(accepted)
        rejected = Counter(flag for r in audited if not r['primary_valid'] for flag in r['flags'])
        results.append(dict(file=path.name, sha256=capture.sha256, reference_m=reference,
                            reference_uncertainty_m=uncertainty, accepted=accepted,
                            raw_rows=len(audited), excluded=sum(not r['primary_valid'] for r in audited),
                            malformed_records=len(capture.malformed), exclusion_flags=dict(sorted(rejected.items())),
                            median_m=centre, bias_m=centre-reference,
                            mean_m=statistics.mean(accepted), sd_m=statistics.pstdev(accepted),
                            mad_m=statistics.median(abs(v-centre) for v in accepted),
                            epochs=len(capture.epochs)))
    correction = -statistics.median(r['bias_m'] for r in results)
    # Firmware accepts signed millimetres. Round once, then use that exact value
    # for every residual. Python round ties to even; record the unrounded result.
    correction_mm = round(correction*1000)
    if not -10000 <= correction_mm <= 10000:
        raise ValueError('proposed correction exceeds firmware bounds; investigate inputs')
    for result in results:
        residuals = [v+correction_mm/1000-result['reference_m'] for v in result['accepted']]
        result.update(residual_median_m=statistics.median(residuals),
                      residual_mean_m=statistics.mean(residuals),
                      residual_p95_abs_m=percentile([abs(v) for v in residuals], 95))
    return correction, correction_mm, rows, results


def build_report(results, correction, correction_mm, uncertainty, max_range):
    lines = ['# Five-distance additive ADS calibration analysis', '',
             'Proposal only: calibration remains disabled in firmware and in the exported configuration.',
             'No scale factor is estimated. References are manually measured antenna separations,',
             'not certified ground truth. Counter accumulation in the 2 m capture is operator-reported',
             'reuse without an ALR reset; accumulated counters are not used as a defect or fit criterion.', '',
             f'Estimator: negative median of per-run median(raw_m - reference_m), {len(results)} equally weighted runs.',
             f'Unrounded correction: {correction:.9f} m; proposed correction_mm={correction_mm}.',
             f'Reference-uncertainty assumption: ±{uncertainty:.3f} m per run; illustrative, not verified or a statistical CI.',
             'A common reference error shifts the proposed correction by the same amount; more samples',
             'do not reduce that systematic uncertainty. Distance-dependent residuals may reflect reference',
             'error, orientation, propagation or radio bias; this analysis cannot identify their cause.', '',
             f'Validity: unique ordered session/transaction RESULTS with preceding six timestamps, independent',
             f'40-bit ADS recomputation, bounded 100 ms intervals, ±1000 ppm ratio, firmware agreement,',
             f'finite/nonnegative raw range, firmware valid status when present, and range ≤{max_range:g} m.',
             'TIMES-only candidates, duplicates, malformed/nonfinite/negative/invalid results are excluded',
             'explicitly and retained in the sample export. MAD/jump annotations alone do not exclude samples.',
             'Existing software offsets/calibrated output are never used as the fit input.', '',
             '| Reference m | Audited valid / rows | Raw median m | Bias m | SD m | MAD m | Corrected median residual m | p95 absolute residual m |',
             '|---:|---:|---:|---:|---:|---:|---:|---:|']
    for r in results:
        lines.append(f"| {r['reference_m']:g} | {len(r['accepted'])}/{r['raw_rows']} | {r['median_m']:.6f} | {r['bias_m']:.6f} | {r['sd_m']:.6f} | {r['mad_m']:.6f} | {r['residual_median_m']:.6f} | {r['residual_p95_abs_m']:.6f} |")
    lines += ['', 'Input audit and exclusions (flag counts may overlap):', '']
    for r in results:
        lines += [f"- `{r['file']}` SHA256 `{r['sha256']}`: {r['epochs']} epoch(s), {r['excluded']} excluded sample rows, {r['malformed_records']} malformed records; flags {json.dumps(r['exclusion_flags'],sort_keys=True)}."]
    lines += ['', 'Residuals reuse the fitting data and are not held-out accuracy validation. Hardware antenna',
              'delays and software offsets must stay unchanged between fitting and validation. The logs',
              'describe one mixed-radio pair/configuration; do not copy its offset to every robot.',
              'Repeat with accurately measured separations, independent sessions, opposite radio role,',
              'and a held-out distance before explicitly enabling calibration. No hardware was flashed.', '']
    return '\n'.join(lines)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--run', action='append', type=run_spec, required=True)
    parser.add_argument('--reference-uncertainty-m', type=finite_positive, required=True)
    parser.add_argument('--max-range-m', type=finite_positive, default=10.)
    parser.add_argument('--report', type=Path, required=True)
    parser.add_argument('--proposal', type=Path, required=True)
    parser.add_argument('--samples', type=Path, required=True)
    args = parser.parse_args(argv)
    inputs = {p.resolve() for _, p in args.run}
    outputs = [p.resolve() for p in (args.report, args.proposal, args.samples)]
    if len(set(outputs)) != len(outputs) or any(p in inputs for p in outputs):
        parser.error('outputs must differ from all inputs and each other')
    try:
        correction, mm, rows, results = fit(args.run, args.reference_uncertainty_m, args.max_range_m)
    except (ValueError, OSError) as error:
        parser.error(str(error))
    proposal = dict(schema_version=1, proposal_only=True, calibration=0, correction_mm=mm,
                    estimator='negative median of equally weighted run median biases',
                    unrounded_correction_m=correction, scale_fit=False,
                    reference_uncertainty_m=args.reference_uncertainty_m,
                    requires_unchanged_antenna_delays=True,
                    inputs=[{k:v for k,v in r.items() if k!='accepted'} for r in results],
                    review_commands=[f'CFG SET correction_mm {mm}', 'CFG SET calibration on'],
                    note='Review commands opt in only after independent validation. Nothing is applied or saved.')
    args.proposal.write_text(json.dumps(proposal, indent=2, sort_keys=True)+'\n')
    args.report.write_text(build_report(results, correction, mm, args.reference_uncertainty_m, args.max_range_m))
    fields = ['reference_m', 'reference_uncertainty_m', *CSV_FIELDS]
    with args.samples.open('w', newline='') as stream:
        writer = csv.DictWriter(stream, fields, extrasaction='ignore', lineterminator='\n')
        writer.writeheader()
        for row in rows:
            writer.writerow(dict(row, flags=';'.join(sorted(row['flags']))))
    print(f'Proposed correction_mm={mm}; {len(rows)} raw audited rows retained. Calibration disabled; proposal only.')


if __name__ == '__main__':
    main()
