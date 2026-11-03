import importlib.util
from pathlib import Path
import unittest
spec = importlib.util.spec_from_file_location('timing', Path(__file__).parents[1]/'analyze_mul_timing.py')
timing = importlib.util.module_from_spec(spec)
spec.loader.exec_module(timing)


def event(cycle, event, why, times):
    return [f'SL1 c={cycle} l=85 e={event} i=132 j=133 k={k} a={times[2*k]:010X} b={times[2*k+1]:010X} w={why} z=2'
            for k in range(4)]


class Timing(unittest.TestCase):
    def test_wrap_worker_duration_and_boundary_fragments(self):
        lines=event(1,3,0,[timing.MASK-638975,638976,1,2,0,0,0,0])
        rows=list(timing.records(lines))
        self.assertEqual(len(rows),1)
        self.assertAlmostEqual(rows[0]['worker_us'],20)
        self.assertEqual(rows[0]['local'],133)
        self.assertEqual(rows[0]['lost'],2)
        self.assertFalse(list(timing.records(lines[1:])))
        self.assertFalse(list(timing.records(lines[:2]+lines[3:])))

    def test_duplicate_k0_and_interleaved_events_do_not_mix(self):
        a=event(7,2,0,[100,100,0,0,0,0,0,0])
        b=event(7,3,1,[99,33,22,10,0,0,0,0])
        rows=list(timing.records(a[:2]+b+a))
        self.assertEqual(len(rows),2)
        self.assertEqual(rows[1]['tx_error_dtu'],0)

    def test_advance_mapping_radio_setup_and_actual_poll(self):
        rows=list(timing.records(event(19,5,0,[100,200,300,400,0,0,0,0])+event(19,6,0,[400,500,100000000,600,0,0,0,0])+event(19,7,0,[1234,1234,0,0,0,0,0,0])))
        self.assertAlmostEqual(rows[0]['software_prepare_us'],200/timing.DTU_US)
        self.assertAlmostEqual(rows[1]['configured_lead_us'],99999500/timing.DTU_US)
        self.assertEqual(rows[2]['poll_error_dtu'],0)
