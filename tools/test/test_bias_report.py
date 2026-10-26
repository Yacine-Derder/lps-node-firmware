import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('bias_report', ROOT/'tools/mul_bias_report.py')
bias = importlib.util.module_from_spec(spec)
spec.loader.exec_module(bias)


class BiasReport(unittest.TestCase):
    def test_fixed_roster_does_not_claim_clock_identification(self):
        with tempfile.TemporaryDirectory() as work:
            path = Path(work)/'fixed.jsonl'
            row = dict(type='tdoa_vector', reference_node=132, slot_ms=10, followers=[
                dict(follower_node=node, slot=index, valid=True, range_diff_mm=value)
                for index, (node, value) in enumerate(((133, -143), (134, -196), (135, -235)), 1)])
            path.write_text(json.dumps(row))
            result = bias.report([path])
            self.assertFalse(result['device_slot_design_full_rank'])
            self.assertIsNone(result['apparent_ppb_from_adjusted_slope'])
            self.assertEqual(len(result['groups']), 3)

    def test_permuted_known_geometry_separates_device_and_slot_terms(self):
        with tempfile.TemporaryDirectory() as work:
            path = Path(work)/'permuted.jsonl'
            metadata = dict(alr_positions_m={'132': [0, 0], '133': [1, 0], '134': [0, 1]}, rlr_ground_truth_m=[.4, .5])
            rows = [dict(type='capture', experiment=metadata)]
            for order in ((133, 134), (134, 133)):
                rows.append(dict(type='tdoa_vector', reference_node=132, slot_ms=10, followers=[
                    dict(follower_node=node, slot=slot, valid=True,
                         range_diff_mm=bias.geometry_difference(metadata, 132, node)+(20 if node == 133 else -30)+4.6*slot*10)
                    for slot, node in enumerate(order, 1)]))
            path.write_text('\n'.join(map(json.dumps, rows)))
            result = bias.report([path])
            self.assertTrue(result['device_slot_design_full_rank'])
            self.assertAlmostEqual(result['adjusted_slot_slope_mm_per_ms'], 4.6)
            self.assertEqual(result['analysis_target'], 'known-geometry residual')

    def test_invalid_followers_do_not_become_zero_measurements(self):
        with tempfile.TemporaryDirectory() as work:
            path = Path(work)/'invalid.jsonl'
            path.write_text(json.dumps(dict(type='tdoa_vector', reference_node=132, slot_ms=10,
                followers=[dict(follower_node=133, slot=1, valid=False, range_diff_mm=None, reason_name='clock_unavailable')])))
            group = bias.report([path])['groups'][0]
            self.assertEqual(group['valid_fraction'], 0)
            self.assertIsNone(group['mean_dd_mm'])
            self.assertEqual(group['invalid_reasons'], {'clock_unavailable': 1})
