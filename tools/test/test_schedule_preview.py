import importlib.util
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('schedule_preview', ROOT/'tools/mul_schedule_preview.py')
planner = importlib.util.module_from_spec(spec)
spec.loader.exec_module(planner)


class SchedulePreview(unittest.TestCase):
    def test_production_preview_owns_every_reservation_and_rejects_infeasible_rates(self):
        config = dict(rlrs=[141, 142], alr_alr=True, alr_rlr=True, reply_ms=5)
        result = planner.preview(config, cycles=4)
        self.assertEqual(result['version'], 2)
        self.assertEqual(result['planner_version'], 2)
        self.assertEqual(result['radio_integration'], dict(lps_aa=True, rlr_receive=True, alr_rlr=True))
        self.assertEqual((result['aa_pairs'], result['ar_pairs']), (3, 8))
        self.assertEqual(result['requested_millihz'], 17500)
        self.assertEqual(sum(len(c['operations']) for c in result['cycles']), 70)
        for cycle in result['cycles']:
            last = result['data_end_us']
            self.assertLessEqual(cycle['reserved_us'], 700000)
            for operation in cycle['operations']:
                self.assertGreaterEqual(operation['start_us'], last)
                self.assertLess(operation['poll_us'], operation['end_us'])
                self.assertLessEqual(operation['end_us'], 980000)
                last = operation['end_us']
        with self.assertRaisesRegex(ValueError, 'exceed guarded airtime'):
            planner.preview(config | dict(reply_ms=20))

    def test_disabled_types_have_no_ranging_reservations(self):
        result = planner.preview({}, first=0xffffffff, cycles=2)
        self.assertEqual([row['cycle'] for row in result['cycles']], [0xffffffff, 0])
        self.assertTrue(all(not row['operations'] and row['ranging_reserved_us'] == 0 for row in result['cycles']))
        self.assertEqual(result['peak_operations'], 0)

    def test_malformed_configs_and_unsafe_timing_fail_before_any_radio_action(self):
        for config in (dict(typo=1), dict(rlrs=[132]), dict(rlrs=[141, 141]), dict(slot_ms=True),
                       dict(alr_rlr=True), dict(reply_ms=5, tx_lead_us=5000), dict(guard_us=101)):
            with self.assertRaises(ValueError):
                planner.preview(config)

    def test_freshness_priorities_are_versioned_and_keep_requested_rates(self):
        c=dict(alrs=[132,133],rlrs=[141],alr_alr=True,alr_rlr=True,reply_ms=5,
               aa_target_millihz=2000,ar_target_millihz=1000,scheduling_policy='freshness',aa_priority=2,ar_priority=1)
        r=planner.preview(c,cycles=2)
        self.assertEqual(r['planner_version'],3)
        self.assertEqual(r['scheduling_policy'],'freshness')
        self.assertEqual(r['freshness_scope'],'planned_reservations')
        self.assertEqual(r['cycles'][0]['operations'][0]['kind'],'alr_alr')
        other=planner.preview(c|dict(ar_priority=3))
        self.assertEqual(other['cycles'][0]['operations'][0]['kind'],'alr_rlr')
        self.assertNotEqual(other['fingerprint'],r['fingerprint'])
        self.assertEqual(sum(len(x['operations']) for x in r['cycles']),8)
        for bad in (dict(scheduling_policy='adaptive'),dict(aa_priority=256),dict(scheduling_policy='fair')):
            with self.assertRaises(ValueError):planner.preview(c|bad)

    def test_aa_delay_preserves_baseline_and_shifts_only_aa(self):
        c=dict(alrs=[132,133],rlrs=[141],slot_ms=10,rx_budget_us=5000,
               alr_alr=True,alr_rlr=True,aa_target_millihz=1000,ar_target_millihz=50)
        baseline=planner.preview(c,first=1,cycles=20)
        delayed=planner.preview(c|dict(aa_start_delay_us=50000),first=1,cycles=20)
        self.assertEqual(baseline['fingerprint'],'826414CC')
        self.assertEqual(delayed['planner_version'],4)
        self.assertEqual(delayed['aa_start_delay_us'],50000)
        self.assertNotEqual(baseline['fingerprint'],delayed['fingerprint'])
        self.assertEqual(delayed['cycles'][0]['operations'][0]['poll_us'],73100)
        for a,b in zip(baseline['cycles'],delayed['cycles']):
            self.assertEqual(len(a['operations']),len(b['operations']))
            for x,y in zip(a['operations'],b['operations']):
                delta=50000 if x['kind']=='alr_alr' else 0
                for key in ('start_us','poll_us','end_us'):
                    self.assertEqual(y[key],x[key]+delta)
        for delay in (-1,388851,2**32-1,2**32):
            with self.assertRaises(ValueError):
                planner.preview(c|dict(aa_start_delay_us=delay))
