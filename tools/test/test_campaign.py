"""Host-only campaign generation exercises the production planner, never hardware."""
import json
from pathlib import Path
import sys
import tempfile
import unittest
ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT/'tools'))
import mul_campaign


class Campaign(unittest.TestCase):
    def test_all_experiment_groups_preflight_and_no_activation_for_invalid_timing(self):
        with tempfile.TemporaryDirectory() as work:
            out = Path(work)/'campaign'
            rows = mul_campaign.prepare(out)
            self.assertEqual(len(rows), 55)
            self.assertEqual({r['id'][0] for r in rows}, set('ABCDEFG'))
            invalid = [r for r in rows if r['preflight'] == 'rejected']
            self.assertEqual([r['id'] for r in invalid], ['B-slot2'])
            self.assertFalse((out/'B-slot2/lps-config.txt').exists())
            for row in rows:
                if row['preflight'] == 'rejected':
                    continue
                commands = (out/row['id']/'lps-config.txt').read_text().splitlines()
                self.assertEqual(commands[0], ':off')
                self.assertFalse(any(c in (':on', ':ARM') for c in commands))
                result = json.loads((out/row['id']/'preview.json').read_text())
                self.assertTrue(result)
                encoded = bytes.fromhex((out/row['id']/'plan.hex').read_text())
                self.assertEqual(len(encoded), 64)
            with self.assertRaises(FileExistsError):
                mul_campaign.prepare(out)
