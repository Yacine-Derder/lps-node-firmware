#!/usr/bin/env python3
"""Prepare a file-only coordinated MUL experiment campaign. Never opens hardware.

Uses the production C planner for every candidate; invalid candidates have no
activation commands. Scripts are review artifacts for a separately authorized
operator campaign, not executed by this tool.
"""
import argparse
import itertools
import json
from pathlib import Path
import shlex
from mul_schedule_preview import encode, preview


def matrix():
    base = dict(cycle_ms=1000, slot_ms=10, revision=1, tx_lead_us=1000,
                rx_budget_us=500, airtime_us=1000, guard_us=100, reply_ms=20,
                max_utilization_percent=70, alrs=[132, 133, 134, 135], rlrs=[141],
                alr_alr=True, alr_rlr=False, aa_target_millihz=200, ar_target_millihz=200,
                scheduling_policy='fair', aa_priority=0, ar_priority=0)
    rows = []
    def add(name, phase, changes=None, **metadata):
        config = base | (changes or {}) | dict(revision=len(rows)+1)
        rows.append(dict(id=name, phase=phase, config=config, clock_estimator='regression',
                         clock_window=24, geometry_source='twr', seconds=300,
                         operator_condition='stationary LOS', **metadata))
    for estimator, window in itertools.product(('median', 'regression'), (8, 24, 32)):
        add(f'A-{estimator}-w{window}', 'clock')
        rows[-1].update(clock_estimator=estimator, clock_window=window)
    for slot in (10, 5, 3, 2):
        add(f'B-slot{slot}', 'spacing', dict(slot_ms=slot))
    for index, followers in enumerate(itertools.permutations((133, 134, 135))):
        add(f'C-permutation{index}', 'device_slot', dict(alrs=[132, *followers]),
            assignment=dict(reference=132, followers=list(followers)))
    for reply in (20, 10, 5):
        add(f'D-reply{reply}', 'twr', dict(reply_ms=reply, alr_rlr=True))
    for count, receivers, aa, ar in itertools.product((2, 4, 8), (1, 2), (False, True), (False, True)):
        add(f'E-n{count}-r{receivers}-aa{int(aa)}-ar{int(ar)}', 'scheduler',
            dict(alrs=list(range(132, 132+count)), rlrs=list(range(141, 141+receivers)),
                 alr_alr=aa, alr_rlr=ar, aa_target_millihz=20, ar_target_millihz=50))
    add('E-freshness-priority', 'scheduler', dict(scheduling_policy='freshness', aa_priority=200,
                                                ar_priority=1, alr_rlr=True))
    for motion, source in itertools.product(('rlr', 'alrs', 'both'), ('twr', 'external', 'none')):
        add(f'F-{motion}-{source}', 'mobile', dict(alr_rlr=True))
        rows[-1].update(geometry_source=source, operator_condition=f'moving {motion}; LOS',
                       pose_provider='external source required; no Pi/ROS implementation in this campaign',
                       ekf='OFF until measured counters and common-time inputs exist; paired host replay available')
    for condition in ('LOS', 'NLOS'):
        add(f'G-{condition}', 'nlos', dict(alr_rlr=True))
        rows[-1].update(operator_condition=f'controlled {condition}; record occluder and path geometry')
    return rows


def setup(config):
    # Preconditions: assigned IDs/modes already correct; initialized stored plan;
    # RF OFF acknowledged. Edits disable both classes before changing timing.
    c = config
    commands = ['off', 'status', 'plan use off', 'plan set aa off', 'plan set ar off',
                'plan set aa_millihz 0', 'plan set ar_millihz 0', 'plan rlrs',
                'plan set policy fair', 'plan set util_percent 80',
                'plan set lead_us 500', 'plan set process_us 100', 'plan set air_us 400',
                'plan set guard_us 100', 'plan set reply_ms 20',
                'plan set slot_ms 3', 'plan set cycle_ms 1000', 'plan set slot_ms 10']
    for field, key in (('lead_us', 'tx_lead_us'), ('process_us', 'rx_budget_us'),
                       ('air_us', 'airtime_us'), ('guard_us', 'guard_us'), ('reply_ms', 'reply_ms'),
                       ('slot_ms', 'slot_ms'), ('cycle_ms', 'cycle_ms'),
                       ('util_percent', 'max_utilization_percent')):
        commands.append(f'plan set {field} {c[key]}')
    commands += ['plan alrs '+' '.join(map(str, c['alrs'])),
                 'plan rlrs '+' '.join(map(str, c['rlrs'])),
                 f'plan set aa_millihz {c["aa_target_millihz"]}',
                 f'plan set ar_millihz {c["ar_target_millihz"]}',
                 f'plan set policy {c["scheduling_policy"]}',
                 f'plan set aa_priority {c["aa_priority"]}', f'plan set ar_priority {c["ar_priority"]}',
                 f'plan set revision {c["revision"]}',
                 'plan set aa '+('on' if c['alr_alr'] else 'off'),
                 'plan set ar '+('on' if c['alr_rlr'] else 'off'), 'plan show']
    return commands


def prepare(destination):
    destination.mkdir(exist_ok=False)
    rows = matrix()
    for row in rows:
        folder = destination/row['id'];folder.mkdir()
        config = row['config']
        (folder/'plan.json').write_text(json.dumps(config, indent=2)+'\n')
        try:
            result = preview(config, first=0, cycles=3)
        except ValueError as error:
            row.update(preflight='rejected', reason=str(error))
            (folder/'REJECTED.txt').write_text(str(error)+'\nNo activation commands generated.\n')
            continue
        row.update(preflight='software_feasible', physical_validation=False,
                   plan_fingerprint=f"{int.from_bytes(encode(config)[60:64], 'little'):08X}",
                   plan_codec=encode(config)[0])
        (folder/'preview.json').write_text(json.dumps(result, indent=2)+'\n')
        (folder/'plan.hex').write_text(encode(config).hex()+'\n')
        commands = setup(config)+[f'cfg set clock_estimator {row["clock_estimator"]}',
                                  f'cfg set clock_window {row["clock_window"]}', 'cfg set clock_compensate on', 'clock show']
        (folder/'lps-config.txt').write_text('\n'.join(':'+command for command in commands+
            [f'geometry source {row["geometry_source"]}', 'pose show', 'plan use on', 'plan show', 'status'])+'\n')
        # Same configuration grammar; source selection is on LPS ALRs only.
        (folder/'stm-config.txt').write_text('\n'.join(commands+['plan use on', 'plan show', 'status'])+'\n')
        metadata = {k: v for k, v in row.items() if k != 'config'} | dict(plan=config,
                    source_time_alignment='UNKNOWN until independently supplied',
                    ground_truth='separate evaluation artifact; never substitute for noisy poses')
        (folder/'metadata.json').write_text(json.dumps(metadata, indent=2)+'\n')
        robots = ' '.join('--robot 192.168.1.'+str(node) for node in config['rlrs'])
        capture = ('# Review only. Run later from epuck-esp32 after explicit hardware authorization.\n'
                   '# Select stationary-rate mode only while every relevant node is stationary.\n'
                   f'timeout --signal=INT {row["seconds"]} python3 tools/uwb_collect.py {robots} --raw --metadata '
                   f'{shlex.quote(str((folder/"metadata.json").resolve()))} --output '
                   f'{shlex.quote(str((folder/"captures").resolve()))} --no-live\n'
                   '# Stop after the documented duration; keep each reconnect/session separate.\n')
        (folder/'capture-command.txt').write_text(capture)
    (destination/'manifest.json').write_text(json.dumps(dict(schema=1, hardware_operated=False,
        configuration_only=True, cases=rows), indent=2)+'\n')
    return rows


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    rows = prepare(args.output)
    print(json.dumps(dict(cases=len(rows), feasible=sum(r['preflight']=='software_feasible' for r in rows),
                         hardware_operated=False, output=str(args.output)), indent=2))


if __name__ == '__main__':
    main()
