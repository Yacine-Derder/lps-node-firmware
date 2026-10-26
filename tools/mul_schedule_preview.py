#!/usr/bin/env python3
"""Preview production-C ranging reservations without connecting to hardware.

LPS runs generated AA reservations; matching STM32/ESP receive and telemetry
paths and assigned ALR-RLR responders are connected (software-tested only).
Target rates are per participant pair in integer milliHz (500 means0.5Hz).
"""
import argparse
import json
from pathlib import Path
import struct
import subprocess
import tempfile
import zlib

ROOT = Path(__file__).resolve().parents[1]


def encode(config):
    defaults = dict(cycle_ms=1000, slot_ms=3, revision=1, tx_lead_us=1000,
                    rx_budget_us=500, airtime_us=1000, guard_us=100,
                    reply_ms=20, max_utilization_percent=70, alrs=[132, 133, 134, 135], rlrs=[],
                    alr_alr=False, alr_rlr=False, aa_target_millihz=500, ar_target_millihz=2000)
    if not isinstance(config, dict) or set(config)-set(defaults):
        raise ValueError('unknown configuration fields')
    c = defaults | config
    for key in ('alr_alr', 'alr_rlr'):
        if type(c[key]) is not bool:
            raise ValueError(f'{key}: boolean required')
    for key in set(defaults)-{'alrs', 'rlrs', 'alr_alr', 'alr_rlr'}:
        maximum = 0xffffffff if key == 'revision' else 65535
        if type(c[key]) is not int or not 0 <= c[key] <= maximum:
            raise ValueError(f'{key}: integer0..{maximum} required')
    for key, minimum, maximum in (('alrs', 2, 8), ('rlrs', 0, 8)):
        if not isinstance(c[key], list) or not minimum <= len(c[key]) <= maximum or any(type(v) is not int or not 0 <= v <= 254 for v in c[key]):
            raise ValueError(f'{key}: {minimum}..{maximum} integer IDs0..254')
    if c['guard_us'] % 100 or not 100 <= c['guard_us'] <= 10000:
        raise ValueError('guard_us:100..10000 step100')
    for key in ('reply_ms', 'max_utilization_percent'):
        if c[key] > 255:
            raise ValueError(f'{key}: uint8 overflow')
    schedule = bytearray(32)
    struct.pack_into('<BBBBHHI', schedule, 0, 2, 3, len(c['alrs']), 0,
                     c['slot_ms'], c['cycle_ms'], c['revision'])
    schedule[12:12+len(c['alrs'])] = bytes(c['alrs'])
    struct.pack_into('<HHHBB', schedule, 20, c['tx_lead_us'], c['rx_budget_us'], c['airtime_us'], c['guard_us']//100, 0)
    struct.pack_into('<I', schedule, 28, zlib.crc32(schedule[:28]))
    data = bytearray(64)
    data[:4] = bytes((2, int(c['alr_alr']) | (int(c['alr_rlr']) << 1), c['reply_ms'], c['max_utilization_percent']))
    data[4:36] = schedule
    data[36] = len(c['rlrs'])
    data[37:37+len(c['rlrs'])] = bytes(c['rlrs'])
    struct.pack_into('<HH', data, 46, c['aa_target_millihz'], c['ar_target_millihz'])
    struct.pack_into('<I', data, 60, zlib.crc32(data[:60]))
    return bytes(data)


def preview(config, first=0, cycles=1):
    data = encode(config)
    if type(first) is not int or not 0 <= first <= 0xffffffff or type(cycles) is not int or not 1 <= cycles <= 100:
        raise ValueError('first:uint32; cycles:1..100')
    with tempfile.TemporaryDirectory() as work:
        executable = Path(work)/'plan'
        core = ROOT/'portable/mul_core'
        subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror', '-O2', '-I'+str(core),
                        str(ROOT/'tools/test/ranging_plan_preview.c'), str(core/'mul_ranging_plan.c'),
                        str(core/'mul_schedule.c'), str(core/'mul_common.c'), '-o', str(executable)], check=True)
        result = subprocess.run([str(executable), str(first), str(cycles)], input=data, capture_output=True)
        if result.returncode:
            raise ValueError(result.stderr.decode().strip())
        return json.loads(result.stdout)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('config', type=Path)
    parser.add_argument('--start-cycle', type=int, default=0)
    parser.add_argument('--cycles', type=int, default=1)
    args = parser.parse_args()
    try:
        result = preview(json.loads(args.config.read_text()), args.start_cycle, args.cycles)
    except ValueError as error:
        parser.error(str(error))
    print(json.dumps(result, indent=2, allow_nan=False))


if __name__ == '__main__':
    main()
