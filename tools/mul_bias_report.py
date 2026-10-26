#!/usr/bin/env python3
"""Read-only device/slot comparison for compact or raw TDoA vector JSONL.

A single fixed roster cannot separate device bias from slot-dependent error.
Optional measured experiment geometry produces residuals; otherwise slopes are
measurement correlations, not identified clock errors. No firmware is modified.
"""
import argparse
from collections import defaultdict
import json
import math
from pathlib import Path
import statistics


def geometry_difference(metadata, reference, follower):
    try:
        positions = metadata['alr_positions_m']
        point = metadata['rlr_ground_truth_m']
        ref, fol = positions[str(reference)], positions[str(follower)]
        if len(point) not in (2, 3) or len(ref) != len(point) or len(fol) != len(point):
            return None
        if not all(math.isfinite(value) for values in (ref, fol, point) for value in values):
            return None
        distance = lambda anchor: math.sqrt(sum((a-b)**2 for a, b in zip(anchor, point)))
        return 1000*(distance(fol)-distance(ref))
    except (KeyError, TypeError, ValueError):
        return None


def solve(rows, size):
    """Rank-checked small least-squares solve for descriptive fixed effects."""
    matrix = [[sum(x[i]*x[j] for x, _ in rows) for j in range(size)] +
              [sum(x[i]*y for x, y in rows)] for i in range(size)]
    for i in range(size):
        pivot = max(range(i, size), key=lambda j: abs(matrix[j][i]))
        matrix[i], matrix[pivot] = matrix[pivot], matrix[i]
        if abs(matrix[i][i]) < 1e-9:
            return None
        scale = matrix[i][i]
        matrix[i] = [value/scale for value in matrix[i]]
        for j in range(size):
            if j != i:
                factor = matrix[j][i]
                matrix[j] = [a-factor*b for a, b in zip(matrix[j], matrix[i])]
    return [matrix[i][-1] for i in range(size)]


def report(paths, metadata_override=None):
    groups = defaultdict(lambda: dict(total=0, values=[], errors=[], reasons=defaultdict(int)))
    offsets, samples = defaultdict(set), []
    references = set()
    for path in paths:
        metadata = metadata_override or {}
        for number, line in enumerate(path.read_text().splitlines(), 1):
            row = json.loads(line)
            if row.get('type') == 'capture':
                metadata = metadata_override or row.get('experiment', {})
            if row.get('kind') == 'experiment':
                metadata = metadata_override or row.get('metadata', {})
            if row.get('type') != 'tdoa_vector' or row.get('sequence_status') in ('old', 'duplicate'):
                continue
            reference, spacing = row['reference_node'], row['slot_ms']
            for follower in row['followers']:
                identity, slot = follower['follower_node'], follower['slot']
                offset = slot*spacing
                key = (str(path), reference, identity, slot, spacing, row.get('schedule_fingerprint'))
                group = groups[key]
                group['total'] += 1
                value = follower.get('range_diff_mm')
                if not follower['valid'] or value is None:
                    group['reasons'][follower.get('reason_name', str(follower.get('reason')))] += 1
                    continue
                if not isinstance(value, (float, int)) or not math.isfinite(value):
                    raise ValueError(f'{path}:{number}: nonfinite measurement')
                truth = geometry_difference(metadata, reference, identity)
                error = None if truth is None else value-truth
                group['values'].append(value)
                if error is not None:
                    group['errors'].append(error)
                references.add(reference)
                offsets[identity].add(offset)
                samples.append((identity, offset, value, error))
    result = []
    for key, group in sorted(groups.items(), key=lambda item: str(item[0])):
        path, reference, identity, slot, spacing, fingerprint = key
        values, errors = group['values'], group['errors']
        result.append(dict(log=path, reference_node=reference, follower_node=identity, slot=slot, slot_ms=spacing,
                           offset_ms=slot*spacing, schedule_fingerprint=fingerprint,
                           total=group['total'], valid=len(values), valid_fraction=len(values)/group['total'],
                           mean_dd_mm=statistics.mean(values) if values else None,
                           sd_dd_mm=statistics.stdev(values) if len(values)>1 else None,
                           mean_error_mm=statistics.mean(errors) if errors else None,
                           rmse_mm=math.sqrt(statistics.mean(e*e for e in errors)) if errors else None,
                           invalid_reasons=dict(group['reasons'])))
    identities = sorted(offsets)
    # Regress only known-geometry residuals if every sample has a valid truth;
    # otherwise explicitly retain the weaker measurement-only interpretation.
    residuals = bool(samples) and all(sample[3] is not None for sample in samples)
    rows = []
    for identity, offset, value, error in samples:
        rows.append(([1, offset]+[int(identity == node) for node in identities[1:]], error if residuals else value))
    fit = solve(rows, 1+len(identities)) if rows and len(references) == 1 else None
    identified = fit is not None
    return dict(groups=result, device_slot_design_full_rank=identified,
                per_device_offsets_ms={str(node): sorted(values) for node, values in offsets.items()},
                references=sorted(references),
                analysis_target='known-geometry residual' if residuals else 'raw range-difference measurement',
                adjusted_slot_slope_mm_per_ms=fit[1] if fit else None,
                apparent_ppb_from_adjusted_slope=fit[1]/.299792458 if fit else None,
                interpretation=('descriptive association; timing/geometry/model causes still require controlled experiments' if identified
                                else 'device and slot effects confounded: permute slot assignments while preserving measured geometry'))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('logs', nargs='+', type=Path)
    parser.add_argument('--metadata', type=Path, help='Optional measured geometry JSON object, overriding capture metadata')
    args = parser.parse_args()
    metadata = json.loads(args.metadata.read_text()) if args.metadata else None
    print(json.dumps(report(args.logs, metadata), indent=2, allow_nan=False))


if __name__ == '__main__':
    main()
