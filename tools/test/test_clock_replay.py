import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('clock_replay', ROOT/'tools/mul_clock_replay.py')
replay = importlib.util.module_from_spec(spec)
spec.loader.exec_module(replay)


class ClockReplay(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.work = tempfile.TemporaryDirectory()
        cls.library = replay.build_library(cls.work.name)

    @classmethod
    def tearDownClass(cls):
        cls.work.cleanup()

    def test_production_clock_compares_fit_and_median_and_rejects_outlier(self):
        model = replay.Replay(self.library)
        try:
            for i in range(60):
                t = i*1000*replay.MS
                row = dict(sequence=i, reference_tx_dtu=(100000+t)&replay.MASK,
                           local_rx_dtu=(800000+t+t*15000//1000000000+(100000 if i == 40 else 0))&replay.MASK,
                           uptime_ms=10000+i*1000)
                value = model.feed(row)
                if i == 40:
                    self.assertEqual(value['fallback'], 3)
                    self.assertNotEqual(value['mapping_status'], 2)
            self.assertTrue(value['fit_ready'])
            self.assertTrue(value['fit_applied'])
            self.assertEqual(value['regression_ppb'], 15000)
            self.assertEqual(value['fitted_ppb'], 15000)
            self.assertEqual(value['rejected'], 1)
            with self.assertRaises(ValueError):
                model.feed(dict(row, local_rx_dtu=1 << 40))
        finally:
            model.close()

    def test_text_parser_preserves_integer_domains_and_resets(self):
        path = Path(self.work.name)/'capture.log'
        path.write_text('MUL ALR: Mode initialized role=FOLLOWER src=0x86\n'
                        'MUL ALR: REF_RX src=0x84 seq=4294967295 tx=0xFFFFFEFFFF rx=0x1234567890 state=ACQUIRED\n'
                        'MUL ALR: REF_RX src=0x84 seq=0 tx=0x0000001000 rx=0x1234580000 state=ACQUIRED\n')
        rows = list(replay.observations(path, 1000))
        self.assertEqual(len(rows), 2)
        self.assertEqual(rows[0]['local'], 134)
        self.assertEqual(rows[0]['local_rx_dtu'], 0x1234567890)
        self.assertTrue(rows[0]['reset'])
        self.assertFalse(rows[1]['reset'])

    def test_quadratic_stays_offline_and_forecasts_held_out_sample(self):
        points = [(i, 2*i*i+3*i+4) for i in range(24)]
        expected = 2*24*24+3*24+4
        quadratic = replay.polynomial_predict(points, 24, 2)
        linear = replay.polynomial_predict(points, 24, 1)
        self.assertAlmostEqual(quadratic, expected, places=7)
        self.assertLess(abs(quadratic-expected), abs(linear-expected))

    def test_clock_json_uses_radio_sequence_and_epoch_instead_of_transport_order(self):
        path = Path(self.work.name)/'clock.jsonl'
        clock = dict(type='clock', node=141, epoch='A', mapping_epoch='B', sequence=999,
                     reference_sequence=42, reference_node=132, reference_tx_dtu=1000,
                     local_rx_dtu=2000, u_ms=10000)
        path.write_text('\n'.join(json.dumps(row) for row in (clock, dict(clock, sequence=1000, reference_sequence=43),
                                                            dict(clock, mapping_epoch='C', sequence=1001, reference_sequence=0))))
        rows = list(replay.observations(path, 1000))
        self.assertEqual([row['sequence'] for row in rows], [42, 43, 0])
        self.assertEqual([row['reset'] for row in rows], [True, False, True])
        self.assertTrue(all(row['local'] == 141 for row in rows))

    def test_capture_age_and_period_follow_production_clock_validity(self):
        model = replay.Replay(self.library)
        try:
            for period in (100, 2000, 500):
                for i in range(35):
                    t = i*period*replay.MS
                    row = dict(sequence=i, reference_tx_dtu=(100000+t)&replay.MASK,
                               local_rx_dtu=(800000+t+t*15000//1000000000)&replay.MASK,
                               uptime_ms=10000+i*period, sample_period_ms=period, capture_age_ms=2)
                    value = model.feed(row)
                self.assertTrue(value['fit_applied'])
                self.assertEqual(value['sample_period_ms'], period)
                self.assertEqual(value['regression_ppb'], 15000)
                invalid = model.feed(dict(row, capture_age_ms=16))
                self.assertEqual(invalid['capture_validity'], 'invalid')
                self.assertEqual(invalid['result'], 0)  # Ignored; does not refresh the prior mapping.
                self.assertEqual(invalid['count'], value['count'])
            legacy = model.feed(dict(row, capture_age_ms=None))
            self.assertIsNone(legacy['capture_valid'])
            for bad in (-1, 65536, True):
                with self.assertRaises(ValueError):
                    model.feed(dict(row, capture_age_ms=bad))
            for bad in (0, 2001, True):
                with self.assertRaises(ValueError):
                    model.feed(dict(row, sample_period_ms=bad))
        finally:
            model.close()
