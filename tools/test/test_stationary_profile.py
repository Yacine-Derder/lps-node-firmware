"""No hardware: profile guards and incremental rebuild dependency graph."""
import pathlib
import subprocess
import unittest
ROOT=pathlib.Path(__file__).resolve().parents[2]
class Profile(unittest.TestCase):
    def test_named_profile_carries_all_options(self):
        out=subprocess.check_output(['make','-n','-f','profiles/stationary-ch5.mk','-W','portable/mul_core/mul_clock_fit.c'],cwd=ROOT,text=True)
        for flag in ('MUL_HYBRID_CH5=1','MUL_PROP_MAX_AGE_MS=6000','MUL_RELATIVE_SPEED_MM_S=0','MUL_STATIONARY_CH5=1'):
            self.assertIn(flag,out)
        self.assertIn('.build-flags',out)
    def test_partial_stationary_flags_fail_compilation(self):
        for flags,success in (([],False),(['-DMUL_PROP_MAX_AGE_MS=6000'],False),(['-DMUL_PROP_MAX_AGE_MS=6000','-DMUL_RELATIVE_SPEED_MM_S=0','-DMUL_HYBRID_CH5=1'],True)):
            result=subprocess.run(['cc','-x','c','-fsyntax-only','-DMUL_STATIONARY_CH5=1',*flags,'portable/mul_core/mul_propagation.h'],cwd=ROOT,capture_output=True)
            self.assertEqual(result.returncode==0,success)
