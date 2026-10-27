#!/usr/bin/env python3
"""Run all native tests for each MUL slot; optionally sanitize every MUL test.

Uses existing GCC/Ruby/Rake/Unity setup, temporary configs and no firmware flash.
"""
import argparse
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]


def run(command, log, env):
    with log.open('w') as output:
        result = subprocess.run(command, cwd=ROOT, env=env, stdout=output, stderr=subprocess.STDOUT)
    if result.returncode:
        print(log.read_text()[-8000:], file=sys.stderr)
        raise SystemExit(f'Failed ({result.returncode}); log: {log}')
    return re.sub(r'\x1b\[[0-9;]*m', '', log.read_text())


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--twr-trace', action='store_true', help='enable bounded generated AA stage trace')
    parser.add_argument('--sanitize', action='store_true')
    parser.add_argument('--mul-only', action='store_true', help='test production profile and only MUL fixtures')
    parser.add_argument('--prop-age', type=int, choices=[1000, 6000], default=1000)
    parser.add_argument('--speed', type=int, default=-1, help='relative ALR separation-speed bound, mm/s; -1 UNKNOWN')
    parser.add_argument('--default-rate', action='store_true', help='test default unavailable-rate policy instead of stationary opt-in')
    parser.add_argument('--slot', type=int, choices=[500, 50, 10], action='append')
    args = parser.parse_args()
    # Keep logs available after completion, outside the source tree.
    work = Path(tempfile.mkdtemp(prefix='mul-slots-'))
    env = os.environ.copy()
    (work / 'python').symlink_to(sys.executable)  # Rake's existing submodule check.
    env['PATH'] = str(work) + os.pathsep + env.get('PATH', '')
    print(f'Logs: {work}', flush=True)
    for slot in args.slot or [500, 50, 10]:
        config = work / f'{slot}.yml'
        ruby = '''require 'yaml'
c=YAML.safe_load(File.read('tools/test/gcc.yml'), permitted_classes: [Symbol], aliases:true)
c['compiler']['defines']['items'] += ["MUL_FOLLOWER_SLOT_MS=#{ARGV[1]}", "MUL_EXPERIMENTAL_STATIONARY_RLR_RATE=#{ARGV[2]}", "MUL_PROP_MAX_AGE_MS=#{ARGV[3]}", "MUL_RELATIVE_SPEED_MM_S=#{ARGV[4]}", "MUL_ONLY=#{ARGV[5]}", "MUL_TWR_TRACE=#{ARGV[6]}"]
File.write(ARGV[0],YAML.dump(c))'''
        subprocess.run(['ruby', '-e', ruby, str(config), str(slot), '0' if args.default_rate else '1', str(args.prop_age), str(args.speed), '1' if args.mul_only else '0', '1' if args.twr_trace else '0'], cwd=ROOT, check=True)
        output = run(['rake', f'config[{config}]', 'clean', 'unit_mul' if args.mul_only else 'unit', 'summary'], work / f'host-{slot}.log', env)
        summary = re.search(r'\d+ TOTAL TESTS \d+ TOTAL FAILURES \d+ IGNORED', output)
        if not summary or '0 TOTAL FAILURES' not in summary[0]:
            raise SystemExit(f'Missing/passing summary not found for {slot}')
        print(f'{slot} ms: {summary[0]}', flush=True)
        observations = [line for line in output.splitlines() if line.startswith('MUL OBS:')]
        if not observations or any(len(line)+2 > 512 for line in observations):
            raise SystemExit('Missing OBS coverage or OBS line exceeds the USB queue')
        for line in observations:
            fields = [part.split('=')[0] for part in line.split()[2:]]
            expected = 'ref fol t_ms rx gen epoch status dtu mm slot_ms delay_dtu delay_age_ms delay_state rate clock_age_ms clock power assoc motion_mm motion_dtu quality uncertainty'.split()
            if fields != expected or not line.endswith('uncertainty=UNKNOWN'):
                raise SystemExit('OBS schema/order changed')
            if 'status=AVAILABLE ' not in line and 'dtu=NA mm=NA' not in line:
                raise SystemExit('Unavailable OBS leaked numerical TDoA')
        print(f'{slot} ms: OBS schema/NA masking/512-byte bound passed', flush=True)
        # Verify compile-time contract, independently of runtime tests.
        subprocess.run(['gcc', '-x', 'c', '-fsyntax-only', f'-DMUL_FOLLOWER_SLOT_MS={slot}',
                        'inc/mul_timing.h'], cwd=ROOT, check=True)
        if args.sanitize:
            total = 0
            for test in sorted((ROOT / 'test').glob('TestMul*.c')):
                # Same explicit dependency convention as the existing Rake suite.
                headers = re.findall(r'^#include "(\w+)\.h"', test.read_text(), re.M)
                sources = [str(ROOT / 'src' / (h + '.c')) for h in headers if (ROOT / 'src' / (h + '.c')).exists()]
                binary = work / f'{test.stem}-{slot}'
                includes = ['inc', 'inc/f0', 'test', 'vendor/unity/src', 'lib/freertos/inc',
                            'hal/stm32f0xx/Inc', 'vendor/libdw1000/inc',
                            'Middlewares/ST/STM32_USB_Device_Library/Class/CDC/Inc',
                            'Middlewares/ST/STM32_USB_Device_Library/Core/Inc']
                command = ['gcc', '-std=c99', '-O1', '-g', '-Wall', '-Wextra', '-Werror',
                           '-Wno-unused-parameter', '-Wno-sign-compare', '-DUNITY_SUPPORT_64',
                           '-DSTM32F072xB', '-DHSI48_VALUE=((uint32_t)48000000)',
                           f'-DMUL_FOLLOWER_SLOT_MS={slot}', f'-DMUL_EXPERIMENTAL_STATIONARY_RLR_RATE={0 if args.default_rate else 1}',
                           f'-DMUL_PROP_MAX_AGE_MS={args.prop_age}', f'-DMUL_RELATIVE_SPEED_MM_S={args.speed}', f'-DMUL_ONLY={1 if args.mul_only else 0}', f'-DMUL_TWR_TRACE={1 if args.twr_trace else 0}',
                           '-fsanitize=address,undefined', '-fno-omit-frame-pointer', '-fno-pie', '-no-pie']
                command += ['-I' + i for i in includes]
                command += [str(test), f'generated-test/build/{test.stem}_Runner.c',
                            'vendor/unity/src/unity.c', *sources, '-lm', '-o', str(binary)]
                run(command, work / f'compile-{test.stem}-{slot}.log', env)
                env['ASAN_OPTIONS'] = 'detect_leaks=1:halt_on_error=1'
                env['UBSAN_OPTIONS'] = 'halt_on_error=1:print_stacktrace=1'
                output = run([str(binary)], work / f'sanitize-{test.stem}-{slot}.log', env)
                total += int(re.search(r'(\d+) Tests 0 Failures', output)[1])
            print(f'{slot} ms: {total} MUL ASan/UBSan tests passed (includes real driver)', flush=True)
    result = subprocess.run(['gcc', '-x', 'c', '-fsyntax-only', '-DMUL_FOLLOWER_SLOT_MS=20',
                             'inc/mul_timing.h'], cwd=ROOT, capture_output=True)
    if result.returncode == 0 or b'MUL_FOLLOWER_SLOT_MS must be' not in result.stderr:
        raise SystemExit('Invalid slot was not rejected as expected')
    print('Unsupported-slot compile rejection passed', flush=True)


if __name__ == '__main__':
    main()
