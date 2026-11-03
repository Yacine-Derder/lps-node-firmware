#!/usr/bin/env python3
"""Decode bounded SL1 TDMA timing records from saved USB text; no device access."""
import argparse
import json
import re
from pathlib import Path

MASK = (1 << 40) - 1
DTU_US = 63897.6
LINE = re.compile(r'SL1 c=(\d+) l=([0-9A-F]{2}) e=(\d+) i=(\d+) j=(\d+) k=([0-3]) a=([0-9A-F]{10}) b=([0-9A-F]{10}) w=(\d+) z=(\d+)')


def delta(a, b):
    value = (a - b) & MASK
    return value - (1 << 40) if value & (1 << 39) else value


def records(lines):
    pending = {}
    for line in lines:
        match = LINE.search(line)
        if not match:
            continue
        c, local, event, initiator, responder, part, a, b, why, lost = match.groups()
        key = tuple(map(int, (c, event, initiator, responder, why))) + (local,)
        part = int(part)
        if part == 0:
            pending[key] = []
        pieces = pending.get(key)
        if pieces is None or len(pieces) != part * 2:
            pending.pop(key, None)
            continue
        pieces.extend((int(a, 16), int(b, 16)))
        if part == 3:
            cycle, event, initiator, responder, why, local = key
            row = dict(cycle=cycle, local=int(local, 16), event=event, why=why,
                       initiator=initiator, responder=responder, lost=int(lost), times=pieces)
            if event == 1:
                row['rx_to_arm_us'] = delta(pieces[5], pieces[0]) / DTU_US if why == 0 else None
                row['first_check_lead_us'] = delta(pieces[6], pieces[3]) / DTU_US if why == 0 else None
                row['prepared_lead_us'] = delta(pieces[6], pieces[4]) / DTU_US if why == 0 else None
                row['required_lead_us'] = pieces[7] / DTU_US
            elif event == 2 and why <= 1:
                row['tx_error_dtu'] = delta(pieces[1], pieces[0])
            elif event == 3 and why == 0:
                row['worker_us'] = delta(pieces[1], pieces[0]) / DTU_US
            elif event == 5 and why == 0:
                row['mapping_us'] = delta(pieces[1], pieces[0]) / DTU_US
                row['software_prepare_us'] = delta(pieces[2], pieces[0]) / DTU_US
                row['prepare_opens_dtu'] = pieces[3]
            elif event == 6:
                row['radio_prepare_us'] = delta(pieces[1], pieces[0]) / DTU_US
                row['configured_lead_us'] = delta(pieces[2], pieces[1]) / DTU_US
                if why == 0:
                    row['armed_lead_us'] = delta(pieces[2], pieces[3]) / DTU_US
            elif event == 7:
                row['poll_error_dtu'] = delta(pieces[1], pieces[0])
            yield row
            del pending[key]


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('logs', type=Path, nargs='+')
    args = parser.parse_args()
    for path in args.logs:
        for row in records(path.read_text(errors='replace').splitlines()):
            print(json.dumps(dict(file=str(path), **row)))
