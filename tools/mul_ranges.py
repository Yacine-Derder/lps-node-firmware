#!/usr/bin/env python3
"""Export bounded MR1 ALR range records to JSONL, without connecting to devices.

Local DW1000 markers are acquisition time, not host arrival time. Epochs are
boot-local; --session must identify a capture/boot segment. This is an export
interface for a future PC geometry estimator, not a localization solver.
"""
import argparse
from contextlib import nullcontext
import json
from pathlib import Path
import re
import sys


def _uint(fields, key, bits):
    value = fields[key]
    if not re.fullmatch(r'[0-9]+', value) or int(value) >= 1 << bits:
        raise ValueError(f'{key}: uint{bits} required')
    return int(value)


def _hex(fields, key, width):
    if not re.fullmatch(rf'[0-9a-fA-F]{{{width}}}', fields[key]):
        raise ValueError(f'{key}: {width} hex digits required')
    return fields[key].upper()


def parse_range(line, session):
    """Return one complete measurement, None for unrelated console lines."""
    prefix, marker, body = line.partition('MR1 ')
    if not marker:
        return None
    if len(marker + body.rstrip('\r\n')) + 2 > 128:
        raise ValueError('MR1 exceeds atomic USB record bound')
    fields = {}
    for token in body.split():
        if token.count('=') != 1:
            raise ValueError('malformed MR1 token')
        key, value = token.split('=')
        if key in fields:
            raise ValueError('duplicate MR1 field')
        fields[key] = value
    base = {'c', 'i', 'j', 'l', 't', 'e', 'p', 'b', 's', 'why'}
    try:
        status = _uint(fields, 's', 1)
        expected = base if status else base | {'mm', 'raw', 'q'}
        if set(fields) != expected:
            raise ValueError('missing/unsupported MR1 fields')
        cycle = _uint(fields, 'c', 32)
        initiator, responder, local = [_uint(fields, k, 8) for k in ('i', 'j', 'l')]
        if max(initiator, responder, local) > 254 or initiator == responder or local not in (initiator, responder):
            raise ValueError('MR1 pair/local identity')
        kind = _uint(fields, 'b', 3)
        if kind not in (1, 4) or (kind == 4 and initiator >= responder):
            raise ValueError('MR1 operation kind/order')
        timestamp = int(_hex(fields, 't', 10), 16)
        epoch = _hex(fields, 'e', 16)
        fingerprint = _hex(fields, 'p', 8)
        if not re.fullmatch(r'[a-z_]+', fields['why']):
            raise ValueError('MR1 failure reason')
        mm = raw = power = None
        if not status:
            if not int(epoch, 16) or fields['why'] != 'ok':
                raise ValueError('MR1 successful measurement epoch/reason')
            mm, raw = [_uint(fields, k, 32) for k in ('mm', 'raw')]
            if max(mm, raw) > 307475:
                raise ValueError('MR1 range exceeds uint16 ToF domain')
            power = bool(_uint(fields, 'q', 1))
    except KeyError as error:
        raise ValueError('missing MR1 field') from error
    return dict(type='alr_range', version=1, capture_session=session,
                kind='reference_follower' if kind == 1 else 'follower_follower',
                initiator=initiator, responder=responder, local_node=local, cycle=cycle,
                plan_fingerprint=fingerprint, valid=not status, range_mm=mm, raw_range_mm=raw,
                power_corrected=power, failure_reason=None if not status else fields['why'],
                physical_uncertainty='UNKNOWN',
                acquisition=dict(clock_domain='dw1000', node=local, epoch=epoch,
                                 timestamp_dtu=timestamp, modulus_bits=40,
                                 marker=('poll_tx' if local == initiator else 'poll_rx') if not status else 'failure',
                                 epoch_scope='local_firmware_boot'))


def geometry_edges(records, alrs):
    """Select Stage A inputs only; retain endpoint/timing provenance and repeats.

    No averaging, cross-clock ordering or snapshot simultaneity is invented.
    RLR ranges and TDoA never enter the ALR geometry input stream.
    """
    if len(alrs) < 2 or len(set(alrs)) != len(alrs):
        raise ValueError('distinct ALR roster required; reference first')
    followers = set(alrs[1:])
    for row in records:
        if not row.get('valid') or row.get('type') != 'alr_range':
            continue
        i, j = row['initiator'], row['responder']
        if ((row['kind'] == 'reference_follower' and i == alrs[0] and j in followers) or
                (row['kind'] == 'follower_follower' and i in followers and j in followers)):
            yield row


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('logs', nargs='+', type=Path)
    parser.add_argument('--session', required=True, help='capture/boot-segment identifier')
    parser.add_argument('--alrs', nargs='+', type=int, help='optional Stage A filter, reference first')
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()

    def records():
        for path in args.logs:
            with path.open() as source:
                for number, line in enumerate(source, 1):
                    try:
                        row = parse_range(line, args.session)
                    except ValueError as error:
                        raise ValueError(f'{path}:{number}: {error}') from error
                    if row is not None:
                        yield row | dict(source_file=str(path), source_line=number)
    try:
        rows = geometry_edges(records(), args.alrs) if args.alrs else records()
        with args.output.open('w') if args.output else nullcontext(sys.stdout) as output:
            for row in rows:
                output.write(json.dumps(row, separators=(',', ':'), allow_nan=False) + '\n')
    except (OSError, ValueError) as error:
        parser.exit(1, str(error) + '\n')


if __name__ == '__main__':
    main()
