"""Compare prepared packet validation against the frozen pre-fix RF5 codec."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
BASELINE = 'ea64edd9f84f0454dfa0b25bca59a9843aba04b9'


class TwrBinding(unittest.TestCase):
    def test_frozen_codec_wire_and_mutation_verdicts(self):
        core = ROOT/'portable/mul_core'
        names = ('mulPlanBroadcastEncode', 'mulPlanBroadcastDecode', 'mulPlanOperationNetwork',
                 'mulPlanTwrEncode', 'mulPlanTwrDecode')
        with tempfile.TemporaryDirectory(prefix='mul-twr-binding-') as temporary:
            work = Path(temporary)
            old = subprocess.check_output(['git', 'show', BASELINE+':portable/mul_core/mul_plan_protocol.c'], cwd=ROOT).decode()
            (work/'frozen.c').write_text(''.join(f'#define {n} frozen_{n}\n' for n in names)+old)
            # The matrix sends all baseline calls to the actual frozen functions;
            # Bound calls go to current production code, not a second reference model.
            (work/'fixture.c').write_text(''.join(f'#define {n} frozen_{n}\n' for n in names)+
                                         f'#include "{core}/tests/test_plan_protocol.c"\n')
            sources = ('mul_plan_protocol', 'mul_ranging_plan', 'mul_multi_protocol',
                       'mul_legacy_twr', 'mul_relation', 'mul_schedule', 'mul_common')
            executable = work/'check'
            subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror', '-O1', '-g',
                            '-fsanitize=address,undefined', '-fno-pie', '-no-pie',
                            '-ffunction-sections', '-Wl,--gc-sections', '-I'+str(core),
                            str(work/'frozen.c'), str(work/'fixture.c'),
                            *(str(core/(s+'.c')) for s in sources), '-lm', '-o', str(executable)], check=True)
            subprocess.run([str(executable)], check=True, stdout=subprocess.DEVNULL)


if __name__ == '__main__':
    unittest.main()
