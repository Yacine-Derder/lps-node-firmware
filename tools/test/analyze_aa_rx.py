#!/usr/bin/env python3
"""Offline AA1/RX1/RX2 correlation. Inputs must cover one boot/run, same plan."""
import argparse
from collections import Counter
from pathlib import Path
import re


def read(path):
    records = {}
    beacons, fits, recovery = {}, {}, Counter()
    current = None
    for line in Path(path).read_text(errors='replace').replace(r'\r\n', '\n').splitlines():
        sequence = re.search(r'(?:REF_RX .*?|TX role=.*?)seq=(\d+)', line)
        if sequence:
            current = int(sequence[1])
        if 'TX role=' in line and current is not None:
            beacons[current] = line
        if 'MUL FIT:' in line and current is not None:
            fits[current] = dict(re.findall(r'(\w+)=([^\s]+)', line))
        if ('RX failure' in line or 'RX timeout' in line or 'SLOT aborted' in line) and current is not None:
            recovery[current] += 1
        match = re.search(r'\b(AA1|RX1|RX2) (.*)', line)
        if not match:
            continue
        fields = dict(re.findall(r'(\w+)=([^\s]+)', match[2]))
        try:
            key = int(fields['c']), int(fields['o'])
            int(fields['l'], 16)
        except (KeyError, ValueError):
            continue
        records.setdefault(key, []).append((match[1], fields))
    return records, beacons, fits, recovery


def has(rows, event, stage=None):
    return any(kind == 'AA1' and int(f['e']) == event and
               (stage is None or int(f['s'], 16) == stage) for kind, f in rows)


def report(reference, follower):
    a, _, _, _ = read(reference)
    b, beacons, fits, recovery = read(follower)
    attempted = [key for key in sorted(a) if has(a[key], 3, 0x10)]
    outcomes = []
    for key in attempted:
        rows, peer = a[key], b.get(key, [])
        result = 'DONE' if has(rows, 6) else 'FAIL' if has(rows, 7) else 'UNKNOWN'
        outcomes.append((key, result))
        snapshots = [f for kind, f in peer if kind == 'RX1']
        radio = ';'.join(f"k{x['k']}:h={x['h']},a={x['a']},z={x['z']}" for x in snapshots) or 'NA'
        fit = fits.get(key[0], {})
        print(f"{key[0]}/{key[1]} {result} poll_rx={int(has(peer,4,0x10))} "
              f"reject={int(has(peer,8))} beacon={int(key[0] in beacons)} "
              f"fit_apply={fit.get('apply','NA')} fit_ppb={fit.get('ppb','NA')} "
              f"recovery_text={recovery[key[0]]} {radio}")
    counts = Counter(x for _, x in outcomes)
    runs, run = [], []
    for key, result in outcomes:
        # Runs are consecutive observed AA attempts, not necessarily cycles:
        # fair plans can have two AA operations or AR-only cycles.
        if result == 'FAIL':
            run.append(key)
        elif run:
            runs.append(run); run = []
    if run:
        runs.append(run)
    print('Attempts:', len(outcomes), dict(counts))
    print('Failure runs (first..last, attempts):', [(r[0], r[-1], len(r)) for r in runs])
    for label, records in (('reference', a), ('follower', b)):
        loss = max((int(f.get('lost', f.get('z', 0))) for rows in records.values() for _, f in rows), default=0)
        print(label, 'max reported diagnostic loss:', loss)
    print('Missing evidence is UNKNOWN; zero loss does not prove an unbroken host capture. '
          'Fit/recovery associations use textual sequence context; RX timestamps are local clocks.')
    return outcomes


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('reference', type=Path)
    parser.add_argument('follower', type=Path)
    args = parser.parse_args()
    report(args.reference, args.follower)
