import importlib.util
import json
import math
from pathlib import Path
import statistics
import subprocess
import sys
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[2]
spec=importlib.util.spec_from_file_location('pose_inputs',ROOT/'tools/mul_pose_inputs.py')
pose=importlib.util.module_from_spec(spec);spec.loader.exec_module(pose)

def trajectory(count):
    return [dict(node=node,source_us=10**12+i*100000,x_mm=x,y_mm=0,z_mm=0)
            for i in range(count) for node,x in ((132,0),(133,10000))]

class PoseInputs(unittest.TestCase):
    def test_seeded_noisy_geometry_replays_actual_c_with_separate_truth_and_wrap(self):
        original=trajectory(2000)
        noisy,truth=pose.generate(original,seed=123,sigma_mm=[50]*3,age_upper_ms=10,local_start_ms=0xfffffff0)
        self.assertEqual(truth,original)
        again,_=pose.generate(original,seed=123,sigma_mm=[50]*3,age_upper_ms=10,local_start_ms=0xfffffff0)
        self.assertEqual(noisy,again)
        self.assertTrue(all(not any(k in row for k in ('x_mm','y_mm','z_mm','truth')) for row in noisy))
        result=pose.replay(noisy,132,133)
        available=[r for r in result if r['geometry_status']==0]
        self.assertEqual(len(available),2000)
        self.assertEqual(result[0]['geometry_status'],1)
        self.assertTrue(any(r['geometry_status']==5 for r in result))
        self.assertTrue(any(r['received_ms']<100 for r in result))
        error=[r['distance_mm']-10000 for r in available]
        rms=math.sqrt(statistics.mean(x*x for x in error))
        self.assertTrue(60<rms<80,rms)
        self.assertTrue(abs(statistics.mean(error))<6)
        for row in available:
            self.assertAlmostEqual(row['variance_mm2'],5000,places=4)
            self.assertEqual(row['physical_uncertainty'],'UNKNOWN')
            self.assertEqual((row['reference_node'],row['follower_node']),(132,133))
            self.assertEqual(row['reference_pose_source_us'],row['follower_pose_source_us'])
            self.assertFalse(row['calibrated'])

    def test_correlated_noise_is_stationary_per_robot_and_inputs_remain_independent(self):
        rows,_=pose.generate(trajectory(5000),seed=77,sigma_mm=[100,0,0],rho=0.8,age_upper_ms=5)
        a=[r['position_mm'][0] for r in rows if r['node']==132]
        b=[r['position_mm'][0]-10000 for r in rows if r['node']==133]
        def correlation(x,y):
            mx,my=statistics.mean(x),statistics.mean(y)
            return sum((u-mx)*(v-my) for u,v in zip(x,y))/math.sqrt(sum((u-mx)**2 for u in x)*sum((v-my)**2 for v in y))
        self.assertTrue(0.76<correlation(a[:-1],a[1:])<0.84)
        self.assertTrue(abs(correlation(a,b))<0.08)
        self.assertTrue(8500<statistics.variance(a)<11500)

    def test_missing_age_reorder_withdrawal_and_frame_mismatch_never_refresh_truth(self):
        unknown,_=pose.generate(trajectory(1),seed=1,sigma_mm=[0]*3)
        self.assertEqual(pose.replay(unknown,132,133)[-1]['geometry_status'],6)
        good,_=pose.generate(trajectory(1),seed=1,sigma_mm=[0]*3,age_upper_ms=0)
        old=dict(good[-1],received_ms=1001)
        result=pose.replay(good+[old],132,133)[-1]
        self.assertEqual((result['ingest_status'],result['geometry_status']),(9,7))
        invalid=dict(good[-1],flags=0,covariance_mm2=[0]*6,sequence=1,received_ms=10)
        invalid['payload_hex']=pose.encode_pose(invalid).hex().upper()
        result=pose.replay(good+[invalid],132,133)[-1]
        self.assertEqual((result['ingest_status'],result['geometry_status']),(0,2))
        foreign=dict(good[-1],frame=4,sequence=1,source_us=good[-1]['source_us']+1000,received_ms=10)
        foreign['payload_hex']=pose.encode_pose(foreign).hex().upper()
        self.assertEqual(pose.replay(good+[foreign],132,133)[-1]['ingest_status'],4)

    def test_cli_artifacts_keep_truth_separate_and_reject_source_aliases(self):
        with tempfile.TemporaryDirectory() as work:
            work=Path(work);source=work/'input.jsonl';truth=work/'truth.jsonl';noisy=work/'noisy.jsonl';commands=work/'commands.txt';out=work/'geometry.jsonl'
            pose.write(source,trajectory(2));original=source.read_bytes()
            cmd=[sys.executable,str(ROOT/'tools/mul_pose_inputs.py'),'generate','--input',str(source),'--truth',str(truth),'--noisy',str(noisy),'--commands',str(commands),'--seed','5','--sigma-mm','20','20','20','--age-upper-ms','5']
            subprocess.run(cmd,check=True)
            self.assertEqual(source.read_bytes(),original)
            self.assertEqual(pose.read(truth),pose.read(source))
            self.assertTrue(commands.read_text().startswith(':pose begin 1 1 1 0\n:pose put '))
            self.assertNotIn(':on',commands.read_text())
            subprocess.run([sys.executable,str(ROOT/'tools/mul_pose_inputs.py'),'replay','--input',str(noisy),'--output',str(out),'--reference','132','--follower','133'],check=True)
            self.assertEqual(len(pose.read(out)),4)
            cmd[cmd.index('--truth')+1]=str(source)
            self.assertNotEqual(subprocess.run(cmd,capture_output=True).returncode,0)
            self.assertEqual(source.read_bytes(),original)
            alias=work/'alias.jsonl';alias.hardlink_to(source)
            cmd[cmd.index('--truth')+1]=str(alias)
            self.assertNotEqual(subprocess.run(cmd,capture_output=True).returncode,0)
            self.assertEqual(source.read_bytes(),original)

    def test_bad_input_timestamps_noise_and_raw_payload_disagreement_fail(self):
        for args in (dict(sigma_mm=[-1,0,0]),dict(sigma_mm=[1]*3,rho=1)):
            with self.assertRaises(ValueError):pose.generate(trajectory(1),seed=1,**args)
        with self.assertRaises(ValueError):pose.generate(trajectory(1)*2,seed=1,sigma_mm=[1]*3)
        rows,_=pose.generate(trajectory(1),seed=1,sigma_mm=[1]*3)
        rows[0]['payload_hex']='00'*80
        with self.assertRaises(ValueError):pose.replay(rows,132,133)
