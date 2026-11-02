import importlib.util
from pathlib import Path
import unittest
from unittest.mock import patch

spec = importlib.util.spec_from_file_location('image', Path(__file__).with_name('check_mul_image.py'))
image = importlib.util.module_from_spec(spec)
spec.loader.exec_module(image)


class ImageLayout(unittest.TestCase):
    def inspect(self, workspace=168, stack=2048, text=100000):
        symbols = dict(workspace=workspace, mulAlrAlgorithm=8, mulRlrAlgorithm=8,
                       mulTwrOnEvent=8, mulClockObserveFitted=8, mulClockFitBegin=8,
                       mulClockFitFinish=8, mulClockFitPrior=8, ucMainStack=1600, txqStorage=512)
        symbols.update({'uwbStaticStack.1': 1600, 'uxIdleTaskStack.1': 800})
        nm = '\n'.join(f'20000000 {size:08x} b {name}' for name, size in symbols.items())
        sections = (f'.isr_vector 192 134217728\n.text {text} 134217920\n'
                    '.data 696 536870912\n.bss 11000 536871608\n'
                    f'._heap 0 536882608\n._stack {stack} 536882608\n')
        with patch.object(image.subprocess, 'check_output', side_effect=[nm, sections]):
            return image.inspect(Path('offline-fixture.elf'))

    def test_current_workspace_and_preserved_stacks(self):
        report = self.inspect()
        self.assertEqual(report['clock_workspace_bytes'], 168)
        self.assertEqual(report['ram_bytes_including_linker_stack'], 696+11000+2048)

    def test_retired_workspace_is_rejected(self):
        with self.assertRaisesRegex(AssertionError, 'workspace'):
            self.inspect(workspace=344)

    def test_stack_reduction_and_flash_overflow_are_rejected(self):
        with self.assertRaisesRegex(AssertionError, 'stack'):
            self.inspect(stack=1024)
        with self.assertRaisesRegex(AssertionError, 'flash overflow'):
            self.inspect(text=131072)


if __name__ == '__main__':
    unittest.main()
