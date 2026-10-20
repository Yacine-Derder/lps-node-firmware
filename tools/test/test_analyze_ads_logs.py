#!/usr/bin/env python3
import contextlib
import io
from pathlib import Path
import tempfile
import unittest
import analyze_ads_logs as ads


def stamps(p=213, db=1000000, da=2000000, a=100, b=90000000, rate=1):
    return [a, (b+round(rate*p))&ads.MASK, (b+round(rate*(p+db)))&ads.MASK,
            (a+db+2*p)&ads.MASK, (a+db+2*p+da)&ads.MASK, (b+round(rate*(3*p+db+da)))&ads.MASK]


def init(sid=1):
    return f'MULH INIT radio=DW1000 role=1 session={sid:016X} ant=16384'


def records(id=1, sid=1, t=None, tagged=False):
    t=stamps() if t is None else t
    _, tof, ratio, metres=ads.recompute(t)
    times=f'MULH TIMES id={id} '+('session='+f'{sid:016X} ' if tagged else '')+' '.join(f't{i+1}={s:010X}' for i,s in enumerate(t))
    result=f'MULH RESULT session={sid:016X} id={id} tof_dtu={float(tof):.6f} ratio={float(ratio):.9f} nominal_m={float(metres):.4f}'
    return times+'\n'+result


def stat(attempt=1, done=1, abort=0, stage=0, **extra):
    f=dict(attempt=attempt,done=done,abort=abort,reject=0,invalid=0,err=3,to=20,stage=stage,**extra)
    return 'MULH STAT '+' '.join(f'{k}={v}' for k,v in f.items())


class Analyzer(unittest.TestCase):
    def parse(self, text, role='initiator'):
        with tempfile.TemporaryDirectory() as d:
            p=Path(d)/'input.log';p.write_text(text)
            capture=ads.parse(p,role)
        rows=ads.analyse(capture)
        return capture,rows

    def test_known_distances_skew_offsets_wrap_and_asymmetry(self):
        for ppm in (-500,-10,0,10,500):
            for distance in (.5,1,2,3,5):
                p=round(distance/float(ads.METRES))
                t=stamps(p, db=6000000000,da=100000,a=ads.MASK-50,b=ads.MASK-100,rate=1+ppm/1e6)
                _,tof,r,_=ads.recompute(t)
                self.assertLess(abs(float(tof)-p*2*(1+ppm/1e6)/(2+ppm/1e6)),1)
                self.assertLess(abs(float(r)-(1+ppm/1e6)),1e-9)
        c,rows=self.parse(init()+'\n'+records(t=stamps(a=ads.MASK-100)))
        self.assertTrue(rows[0]['primary_valid']);self.assertGreater(rows[0]['wrap_intervals'],0)

    def test_negative_retained_next_good_not_excluded_for_jump(self):
        c,rows=self.parse(init()+'\n'+records(t=stamps(p=-213))+'\n'+records(id=2))
        self.assertIn('negative',rows[0]['flags']);self.assertFalse(rows[0]['primary_valid'])
        self.assertIn('large_jump',rows[1]['flags']);self.assertTrue(rows[1]['primary_valid'])
        self.assertEqual(c.epochs[0].analysis['raw']['n'],2)

    def test_negative_near_zero_not_hidden_by_print_rounding(self):
        t=[0,0,1000001,1000000,2000001,2000003]
        _,rows=self.parse(init()+'\n'+records(t=t))
        self.assertEqual(float(rows[0]['tof_dtu']),0)
        self.assertLess(rows[0]['recomputed_tof_dtu'],0)
        self.assertIn('negative',rows[0]['flags']);self.assertFalse(rows[0]['primary_valid'])
        _,rows=self.parse(init()+'\n'+records(t=t)+' range_valid=1')
        self.assertIn('firmware_validity_mismatch',rows[0]['flags'])

    def test_duplicate_ambiguity(self):
        _,rows=self.parse(init()+'\n'+records()+'\n'+records())
        self.assertEqual(len(rows),2)
        self.assertTrue(all('ambiguous_association' in r['flags'] for r in rows))

    def test_sessions_and_no_cross_session_times_join(self):
        c,rows=self.parse(init()+'\n'+records()+'\n'+init(2)+'\n'+records(sid=2,tagged=True))
        self.assertEqual(len(c.epochs),2);self.assertTrue(all(r['primary_valid'] for r in rows))
        oldtimes=records().splitlines()[0];newresult=records(sid=2).splitlines()[1]
        c,rows=self.parse(init()+'\n'+oldtimes+'\n'+newresult)
        self.assertEqual(len(c.epochs),2);self.assertIn('missing_times',next(r for r in rows if r['record_kind']=='RESULT')['flags'])
        self.assertIn('TIMES without RESULT=1',ads.report([c]))

    def test_counter_reset_wrap_and_unknown_responder(self):
        c,_=self.parse(stat(10,10)+'\n'+stat(),role='responder')
        self.assertEqual(len(c.epochs),2);self.assertEqual(c.epochs[1].origin,'counter_reset')
        self.assertTrue(all(e.session=='unknown' for e in c.epochs))
        c,_=self.parse(stat(0xfffffffe,0xfffffffe)+'\n'+stat())
        self.assertEqual(len(c.epochs),1)
        self.assertTrue(any('wrap' in n for n in c.epochs[0].notes))

    def test_concatenated_truncated_ansi_crlf(self):
        text='\x1b[31m'+init()+'\x1b[0m\r\nMULH STAT attempt=2 done=0'+stat()+'\r\n'+records()
        c,rows=self.parse(text)
        self.assertEqual(len(c.malformed),1);self.assertEqual(c.concatenated,[2])
        self.assertEqual(len(c.epochs[0].snapshots),1);self.assertTrue(rows[0]['primary_valid'])

    def test_same_line_order_duplicate_fields_and_classified_abort(self):
        times,result=records().splitlines()
        _,rows=self.parse(init()+'\n'+times+result)
        self.assertTrue(rows[0]['primary_valid'])
        _,rows=self.parse(init()+'\n'+result+times)
        self.assertIn('ambiguous_association',rows[0]['flags'])
        c,rows=self.parse(init()+'\n'+records()+' id=2')
        self.assertTrue(any(m[1]=='duplicate_fields' for m in c.malformed))
        c,_=self.parse('MULH ABORT session=0000000000000001 id=1 cause=2 stage=3 by_stage=0,0,1,0,0,0,0')
        self.assertIn('Visible classified abort records=1',ads.report([c]))

    def test_malformed_nonfinite_and_fw_invalid(self):
        c,rows=self.parse(init()+'\n'+records().replace('t1=0000000064','t1=10000000000'))
        self.assertIn('timestamp_width',rows[0]['flags'])
        c,rows=self.parse(init()+'\n'+records().replace('tof_dtu=213.000000','tof_dtu=nan'))
        self.assertIn('nonfinite',rows[0]['flags']);self.assertEqual(c.epochs[0].analysis['raw']['n'],0)
        _,rows=self.parse(init()+'\n'+records()+' range_valid=0')
        self.assertIn('firmware_invalid',rows[0]['flags'])

    def test_independent_mismatch_rejection_and_no_silent_replacement(self):
        _,rows=self.parse(init()+'\n'+records().replace('tof_dtu=213.000000','tof_dtu=214.000000').replace('ratio=1.000000000','ratio=1.000010000'))
        self.assertIn('tof_mismatch',rows[0]['flags']);self.assertIn('ratio_mismatch',rows[0]['flags'])
        self.assertEqual(rows[0]['tof_dtu'],'214.000000');self.assertEqual(rows[0]['recomputed_tof_dtu'],213)

    def test_missing_incomplete_ordering_and_out_of_order(self):
        c,rows=self.parse(init()+'\n'+records().splitlines()[0])
        self.assertEqual(len(rows),1);self.assertEqual(rows[0]['record_kind'],'TIMES_ONLY');self.assertFalse(rows[0]['primary_valid']);self.assertEqual(c.epochs[0].analysis['unmatched_times'],1)
        _,rows=self.parse(init()+'\n'+records().splitlines()[1])
        self.assertIn('missing_times',rows[0]['flags'])
        c,rows=self.parse(init()+'\n'+'\n'.join(reversed(records().splitlines())))
        self.assertIn('ambiguous_association',rows[0]['flags'])
        _,rows=self.parse(init()+'\n'+records(id=2)+'\n'+records(id=1))
        self.assertIn('out_of_order_result',rows[1]['flags'])
        _,rows=self.parse(init()+'\n'+records(id=0xffffffff)+'\n'+records(id=1))
        self.assertIn('out_of_order_result',rows[1]['flags'])

    def test_robust_rule_and_physical_bound(self):
        text=init()+'\n'+'\n'.join(records(id=i+1,t=stamps(p=p)) for i,p in enumerate([213]*10+[426,10000]))
        c,rows=self.parse(text)
        self.assertIn('range_mad_outlier',rows[-2]['flags']);self.assertIn('outside_lab_bound',rows[-1]['flags'])
        self.assertEqual(c.epochs[0].analysis['robust']['n'],10)

    def test_events_timing_and_counter_consistency(self):
        c,_=self.parse(stat(2,1)+'\n[EPUCK] FAULT uart=5 bits=0x8 drops=14\n[MOTOR] OK STOP\n[CONSOLE] 7 messages dropped (output queue full)\nMULH TIME hz=1000000 spi_off_cycles=300 service_cycles=600 gap_cycles=1000 calls=10')
        text=ads.report([c])
        self.assertIn('attempt != done+abort+active',text)
        self.assertIn('Visible UART fault reports=1; STOP acknowledgements=1',text)
        self.assertIn('message drops=7',text);self.assertIn('spi_off_cycles=300',text)

    def test_statistics_empty_single_and_percentiles(self):
        self.assertEqual(ads.summary([]),{'n':0})
        self.assertEqual(ads.summary([1])['sd_population'],0)
        self.assertEqual(ads.percentile([1,2,3,4],95),3.85)
        for t in ([0]*6,stamps()[:-1],stamps(a=1<<40),[0,1,2,ads.MAX_INTERVAL+1,ads.MAX_INTERVAL+2,3]):
            with self.assertRaises(ValueError):ads.recompute(t)

    def test_closed_cohort_requires_counter_conservation(self):
        text=init()+'\n'+stat(1,0,0,1)+'\n'+records()+'\n'+stat(2,1,0,1)+'\n'+stat(3,1,1,1)+'\n'+records(id=3)
        c,_=self.parse(text)
        cohort=ads.closed_initiator_cohort(c,c.epochs[0])
        self.assertEqual(cohort,dict(first_id=1,last_id=3,completed=2,aborted=1,attempts=3))
        c.epochs[0].snapshots[-1]['done']+=1
        self.assertIsNone(ads.closed_initiator_cohort(c,c.epochs[0]))

    def test_deterministic_cli_and_input_protection(self):
        with tempfile.TemporaryDirectory() as d:
            p=Path(d);(p/'i.log').write_text(init()+'\n'+records())
            args=['--initiator',str(p/'i.log'),'--csv',str(p/'samples.csv'),'--report',str(p/'report.md')]
            with contextlib.redirect_stdout(io.StringIO()):
                ads.main(args);first=((p/'samples.csv').read_bytes(),(p/'report.md').read_bytes())
                ads.main(args)
            self.assertEqual(first,((p/'samples.csv').read_bytes(),(p/'report.md').read_bytes()))
            with contextlib.redirect_stderr(io.StringIO()), self.assertRaises(SystemExit):
                ads.main(['--initiator',str(p/'i.log'),'--csv',str(p/'i.log'),'--report',str(p/'r.md')])


if __name__=='__main__':unittest.main()
