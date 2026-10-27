import contextlib
import importlib.util
import io
from pathlib import Path
import tempfile
import unittest

spec = importlib.util.spec_from_file_location('analyze_aa_rx', Path(__file__).with_name('analyze_aa_rx.py'))
analyzer = importlib.util.module_from_spec(spec)
spec.loader.exec_module(analyzer)


class AnalyzeAaRx(unittest.TestCase):
    def test_multiple_operations_gaps_unknown_and_escaped_serial_lines(self):
        def aa(c, o, e, s):
            return f'AA1 c={c} o={o} p=84>85 l=84 e={e} s={s} lost=0\\r\\n'
        with tempfile.TemporaryDirectory() as directory:
            ref, fol = Path(directory)/'ref', Path(directory)/'fol'
            ref.write_text(aa(9,0,3,'10')+aa(9,0,7,'11')+aa(9,1,3,'10')+aa(9,1,6,'14')+
                           aa(11,0,3,'10'))
            fol.write_text('MUL ALR: REF_RX src=0x84 seq=9\nMUL FIT: ppb=20 apply=1\n'
                           'MUL ALR: TX role=FOLLOWER src=0x85 seq=9\n'+aa(9,1,4,'10')+
                           'RX1 c=9 o=1 l=85 k=1 n=0000000001 h=00050500 a=04 z=2\n')
            output = io.StringIO()
            with contextlib.redirect_stdout(output):
                results = analyzer.report(ref, fol)
            self.assertEqual(results, [((9,0),'FAIL'), ((9,1),'DONE'), ((11,0),'UNKNOWN')])
            self.assertIn('h=00050500', output.getvalue())
            self.assertIn('fit_apply=1', output.getvalue())
            self.assertIn('follower max reported diagnostic loss: 2', output.getvalue())
