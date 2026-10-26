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
        self.assertEqual(result['radio_integration'], dict(lps_aa=True, rlr_receive=False, alr_rlr=False))
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
