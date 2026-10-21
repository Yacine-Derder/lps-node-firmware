import contextlib
import io
import json
from pathlib import Path
import tempfile
import unittest
from unittest import mock
import calibrate_ads as cal
from test_analyze_ads_logs import init, records, stamps


class Calibration(unittest.TestCase):
    def test_real_audit_negative_exclusion_and_sample_export(self):
        with tempfile.TemporaryDirectory() as root:
            root=Path(root);source=root/'raw.log'
            source.write_text(init()+'\n'+records(id=1)+'\n'+records(id=2,t=stamps(p=-213)))
            outputs=[root/'report.md',root/'proposal.json',root/'samples.csv']
            args=['--run',f'0.5={source}','--reference-uncertainty-m','.1','--report',str(outputs[0]),'--proposal',str(outputs[1]),'--samples',str(outputs[2])]
            with contextlib.redirect_stdout(io.StringIO()): cal.main(args)
            proposal=json.loads(outputs[1].read_text())
            self.assertEqual(proposal['calibration'],0);self.assertFalse(proposal['scale_fit'])
            self.assertLess(proposal['correction_mm'],0)
            self.assertEqual(proposal['inputs'][0]['excluded'],1)
            self.assertIn('negative',outputs[2].read_text())
            self.assertIn('not verified',outputs[0].read_text())
            self.assertIn('not held-out',outputs[0].read_text())
            before=source.read_bytes();args[args.index('--report')+1]=str(source)
            with contextlib.redirect_stderr(io.StringIO()), self.assertRaises(SystemExit):cal.main(args)
            self.assertEqual(source.read_bytes(),before)

    def test_runs_weighted_equally_not_by_number_of_samples(self):
        with tempfile.TemporaryDirectory() as root:
            paths=[Path(root)/f'{n}.log' for n in range(3)]
            for path in paths:path.write_text('')
            # First (large) run has a grossly different bias. It has ONE vote.
            values=iter(([100.]*100,[2.5,2.5],[3.5,3.5]))
            def fake(*args,**kwargs):return [dict(record_kind='RESULT',primary_valid=True,value=v,flags=set()) for v in next(values)]
            with mock.patch.object(cal,'analyse',fake):
                correction,mm,_,result=cal.fit(list(zip((1.,2.,3.),paths)),.1,200)
            self.assertEqual(correction,-.5);self.assertEqual(mm,-500)
            self.assertEqual(result[1]['residual_median_m'],0)
            self.assertEqual(result[2]['residual_p95_abs_m'],0)

    def test_bad_runs_numbers_and_duplicate_paths(self):
        for value in ('nan','inf','-1','0'):
            with self.assertRaises(Exception):cal.finite_positive(value)
        with tempfile.TemporaryDirectory() as root:
            path=Path(root)/'raw.log';path.write_text(init()+'\n'+records(t=stamps(p=-213)))
            with self.assertRaisesRegex(ValueError,'no timestamp-audited'):cal.fit([(1.,path)],.1,10)
            path.write_text(init()+'\n'+records())
            with self.assertRaisesRegex(ValueError,'unique'):cal.fit([(1.,path),(2.,path)],.1,10)

    def test_bias_sign_rounding_and_raw_only(self):
        with tempfile.TemporaryDirectory() as root:
            path=Path(root)/'raw.log';path.write_text(init()+'\n'+records()+'\nMULH CAL raw_m=1 calibrated_m=100 calibration=1')
            correction,mm,_,result=cal.fit([(2.,path)],.1,10)
            self.assertGreater(correction,0);self.assertGreater(mm,0)
            self.assertLess(abs(result[0]['residual_median_m']),.000501)


if __name__=='__main__':unittest.main()
