import importlib.util
import itertools
import math
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('mul_ranges', ROOT/'tools/mul_ranges.py')
ranges = importlib.util.module_from_spec(spec)
spec.loader.exec_module(ranges)


class RangeExport(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.work = tempfile.TemporaryDirectory()
        cls.addClassCleanup(cls.work.cleanup)
        cls.binary = Path(cls.work.name)/'range-log'
        subprocess.run(['cc', '-std=c99', '-Wall', '-Wextra', '-Werror',
                        '-Iinc', '-Iinc/f0', '-Ilib/freertos/inc',
                        'tools/test/range_log_fixture.c', 'src/mul_log.c', '-o', str(cls.binary)],
                       cwd=ROOT, check=True)

    def record(self, i, j, tof, cycle=3, failed=0, responder=0, power=0):
        output = subprocess.check_output([str(self.binary), *map(str, (i,j,tof,cycle,failed,responder,power))], text=True)
        lines = output.splitlines()
        self.assertTrue(all(len(line)+2 <= 128 for line in lines))
        rows = [r for line in lines if (r := ranges.parse_range(line, 'synthetic-boot'))]
        self.assertEqual(len(rows),1)
        return rows[0], output

    def test_six_k4_edges_from_production_formatter_without_rlr_geometry_factors(self):
        roster = [132,134,133,135]
        for positions in ({132:(0,0),133:(1200,0),134:(200,900),135:(1500,700)},
                          {132:(-400,200),133:(900,-300),134:(-200,-700),135:(200,1200)}):
            records=[]
            for c,(i,j) in enumerate(itertools.combinations(sorted(roster),2)):
                mm=math.dist(positions[i],positions[j])
                tof=round(mm*63897600000/299792458000)
                row,_=self.record(i,j,tof,cycle=c)
                self.assertLessEqual(abs(row['range_mm']-mm),3)
                self.assertEqual(row['acquisition']['timestamp_dtu'],0x123456789a)
                self.assertEqual(row['acquisition']['epoch'],'FEDCBA9876543210')
                self.assertEqual(row['plan_fingerprint'],'EC2B4B2A')
                records.append(row)
            records += [dict(type='tdoa_vector',valid=True),dict(records[0],responder=141),
                        dict(records[0],valid=False),dict(records[0],kind='alr_rlr')]
            edges=list(ranges.geometry_edges(records,roster))
            self.assertEqual(len(edges),6)
            self.assertEqual({(e['initiator'],e['responder']) for e in edges},set(itertools.combinations(sorted(roster),2)))

    def test_responder_acquisition_and_failure_never_become_host_time_or_a_range(self):
        row,_=self.record(133,134,320,cycle=0xffffffff,responder=1,power=1)
        self.assertEqual(row['local_node'],134)
        self.assertEqual(row['acquisition']['marker'],'poll_rx')
        self.assertTrue(row['power_corrected'])
        failed,text=self.record(133,134,320,failed=1)
        self.assertFalse(failed['valid']);self.assertIsNone(failed['range_mm'])
        self.assertEqual(failed['failure_reason'],'late_setup')
        self.assertEqual(failed['acquisition']['marker'],'failure')
        self.assertEqual(list(ranges.geometry_edges([failed],[132,134,133,135])),[])
        with self.assertRaises(ValueError):ranges.parse_range(text[text.index('MR1 '):].replace('s=1','s=0'),'boot')

    def test_malformed_and_ambiguous_records_fail_closed(self):
        _,text=self.record(133,134,1)
        line=next(line for line in text.splitlines() if line.startswith('MR1 '))
        for bad in (line+' c=4',line.replace('l=133','l=135'),line.replace('b=4','b=2'),
                    line.replace('i=133 j=134','i=134 j=133'),line.replace('q=0','q=2'),
                    line.replace('s=0','s=2'),line.replace('e=FEDCBA9876543210','e=0000000000000000')):
            with self.assertRaises(ValueError):ranges.parse_range(bad,'boot')
        self.assertIsNone(ranges.parse_range('LOG kalman=20','boot'))
