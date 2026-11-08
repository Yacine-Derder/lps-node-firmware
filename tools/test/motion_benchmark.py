#!/usr/bin/env python3
"""Matched production-C numerical controls; stdout JSON, no recordings modified.

The independent-delay comparison exercises an existing API with an ideal,
acquisition-aligned synthetic external input. No such RLR input is wired today.
No output is a physical accuracy estimate, CPU WCET, or localization solver test.
"""
import csv
import io
import json
import math
from pathlib import Path
import statistics
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
NAMES = ['stationary_zero', 'stationary_skew', 'stationary_noise', 'stationary_loss',
         'radial_toward', 'radial_away', 'tangential', 'two_alrs', 'four_alrs',
         'translation', 'rotation', 'geometry_step', 'near_collinear', 'degenerate',
         'moving_rlr', 'moving_both', 'radial_rlr', 'accelerating_rlr']


def stats(values):
    x = sorted(values)
    return dict(n=len(x), median=statistics.median(x),
                rmse=math.sqrt(statistics.mean(v*v for v in x)),
                p95_abs=sorted(abs(v) for v in x)[math.ceil(.95*len(x))-1],
                max_abs=max(abs(v) for v in x))


def main():
    core = ROOT/'portable/mul_core'
    with tempfile.TemporaryDirectory(prefix='mul-motion-') as temp:
        out = Path(temp)/'benchmark'
        sources = ['mul_clock_fit', 'mul_clock', 'mul_relation', 'mul_common',
                   'mul_downlink', 'mul_observation']
        subprocess.run(['cc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror',
                        '-fsanitize=undefined', '-DMUL_RX_MOTION_GUARD=1',
                        '-DMUL_PROP_MAX_AGE_MS=6000', '-I'+str(core),
                        str(ROOT/'tools/test/motion_benchmark.c'),
                        *[str(core/(s+'.c')) for s in sources], '-lm', '-o', str(out)], check=True)
        rows = list(csv.DictReader(io.StringIO(subprocess.check_output([str(out)], text=True))))
    report = dict(model='production C; matched synthetic radio events; ideal independent-delay control',
                  physical_accuracy=False, cases={})
    for case, name in enumerate(NAMES, 1):
        all_rows = [r for r in rows if int(r['scenario']) == case]
        r = [x for x in all_rows if x['slot_ms'] == '50']
        metrics = {key: stats([float(x[key]) for x in r]) for key in
                   ('legacy_ppb', 'independent_ppb', 'legacy_error_mm',
                    'independent_error_mm', 'slot_model_error_mm', 'delay_hold_error_mm')}
        metrics['samples_all_spacings'] = len(all_rows)
        metrics['guard_rejection_rate'] = 1.0
        metrics['guard_numeric_availability'] = 0.0
        metrics['motion_budget_availability'] = sum(int(x['motion_budget_accept']) for x in r)/len(r)
        report['cases'][name] = metrics
    assert report['cases']['stationary_zero']['legacy_ppb']['max_abs'] == 0
    assert report['cases']['stationary_skew']['legacy_ppb']['max_abs'] == 0
    # Noise is common to both paths. Exact independent path input must not
    # change stationary controls, including missing reference intervals.
    for name in NAMES[:4]:
        assert report['cases'][name]['legacy_error_mm'] == report['cases'][name]['independent_error_mm']
    for name in ('radial_toward', 'radial_away', 'translation', 'moving_rlr', 'moving_both'):
        assert report['cases'][name]['independent_ppb']['rmse'] < report['cases'][name]['legacy_ppb']['rmse']
    assert report['cases']['moving_both']['slot_model_error_mm']['rmse'] > 1
    print(json.dumps(report, indent=2, sort_keys=True))


if __name__ == '__main__':
    main()
