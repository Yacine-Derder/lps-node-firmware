"""Dry-run only. This test never invokes dfu-util or accesses a device."""
import pathlib
import subprocess
import unittest
ROOT=pathlib.Path(__file__).resolve().parents[2]
class DfuDependencies(unittest.TestCase):
    def test_current_artifact_is_built_before_dfu(self):
        text=subprocess.check_output(['make','-n','-W','portable/mul_core/mul_clock_fit.c','dfu'],cwd=ROOT,text=True)
        self.assertLess(text.index('arm-none-eabi-gcc'),text.index('dfu-convert.py'))
        self.assertLess(text.index('dfu-convert.py'),text.rindex('dfu-util'))
        self.assertIn('-D bin/lps-node-firmware.dfu',text)
