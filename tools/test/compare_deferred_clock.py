#!/usr/bin/env python3
"""Bit-for-bit old vs deferred posterior trajectories using actual C functions.

Frozen baseline comes from local Git history; no network/hardware access. The
staged fixture also validates current origin, prior covariance bound and an
idempotent second Finish. Output stays in temporary directories.
"""
import argparse
import json
import re
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
BASELINE = 'de20a760636c8df1902a5d938b84247039f19d7e'


def kernel_body(source, name):
    start = re.search(r'\b'+name+r'\([^;]*?\)\s*\{', source).end()-1
    depth = 0
    for end in range(start, len(source)):
        depth += (source[end] == '{') - (source[end] == '}')
        if not depth:
            return source[start:end+1]
    raise AssertionError('unbalanced kernel '+name)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--sanitize', action='store_true')
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='mul-clock-compare-') as directory:
        work = Path(directory)
        baseline = work/'baseline.c'
        baseline.write_bytes(subprocess.check_output(['git', 'show', BASELINE+':portable/mul_core/mul_clock_fit.c'], cwd=ROOT))
        current = (ROOT/'portable/mul_core/mul_clock_fit.c').read_text()
        for kernel in ('healthy', 'predict', 'update', 'seed', 'recover'):
            assert kernel_body(baseline.read_text(), kernel) == kernel_body(current, kernel), 'Kalman kernel changed: '+kernel
        for name, source in (('baseline', baseline), ('deferred', ROOT/'portable/mul_core/mul_clock_fit.c')):
            command = ['gcc', '-std=c11', '-O1', '-g', '-Wall', '-Wextra', '-Werror',
                       '-DMUL_PROP_MAX_AGE_MS=6000', '-DCLOCK_COST_DEFERRED='+str(int(name=='deferred')),
                       '-Iportable/mul_core', 'tools/test/clock_deferred_compare.c', str(source),
                       'portable/mul_core/mul_clock.c', 'portable/mul_core/mul_common.c', '-lm', '-o', str(work/name)]
            if args.sanitize:
                command += ['-fsanitize=address,undefined', '-fno-pie', '-no-pie']
            subprocess.run(command, cwd=ROOT, check=True)
        count = 0
        metrics = []
        for period in (200, 1000):
            for compensate in (0, 1):
                for method in (0, 2):
                    for mode in range(6):
                        runs = [subprocess.run([str(work/name), str(period), str(compensate), str(method), str(mode)],
                                               capture_output=True, check=True)
                                for name in ('baseline', 'deferred')]
                        values = [r.stdout for r in runs]
                        if runs[1].stderr:
                            metrics.append(json.loads(runs[1].stderr))
                        assert values[0] == values[1], f'posterior changed: {(period, compensate, method, mode)}, first differing byte '+str(next(i for i,(a,b) in enumerate(zip(*values)) if a!=b))
                        count += 5000
        print(f'{count} observations: bit-identical complete clock/Kalman state vs {BASELINE[:7]}; '
              '200/1000ms, median/Kalman, compensation changes, wrap, gaps/recovery, drift, outliers and corrupt state passed')
        print(json.dumps(dict(trajectory_accuracy=metrics), indent=2))


if __name__ == '__main__':
    main()
