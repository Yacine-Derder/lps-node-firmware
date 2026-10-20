import unittest
from analyze_phy_logs import analyze


class PhyLogTests(unittest.TestCase):
    def test_wrap_loss_duplicate_and_error_reporting(self):
        tx='\n'.join(f'PHY TX src=84 seq={n}\nPHY DONE src=84 seq={n} done=1' for n in [4294967295,0,1])
        rx=('PHY RX src=84 seq=4294967295 valid=1\nPHY RX src=84 seq=1 valid=1\n'
            'PHY RX src=84 seq=1 valid=1\nPHY STAT valid=3 gaps=1 err=2 to=5\n'
            'PHY ERR status=00008000 crc=2 phr_rs=0')
        result=analyze(tx,rx,'84',2)
        self.assertEqual(result['missing_sequences'],[0])
        self.assertEqual(result['sequence_gaps'],1)
        self.assertEqual(result['duplicate_rx_records'],1)
        self.assertEqual(result['tx_completions'],3)
        self.assertEqual(result['rx_last_counters']['err'],'2')
        self.assertTrue(result['minimum_completed_met'])

    def test_missing_tx_log_or_reset_fails_closed(self):
        for tx in ['', 'PHY TX src=84 seq=1\nPHY TX src=84 seq=1',
                   'PHY TX src=84 seq=1\nPHY TX src=84 seq=3']:
            with self.assertRaises(ValueError):
                analyze(tx,'','84')

    def test_minimum_and_direction_gate(self):
        tx='PHY TX src=86 seq=0\nPHY DONE src=86 seq=0 done=1'
        result=analyze(tx,'PHY RX src=84 seq=0 valid=1','86')
        self.assertFalse(result['minimum_completed_met'])
        self.assertFalse(result['reception_demonstrated'])
        self.assertEqual(result['missing_sequences'],[0])


if __name__=='__main__':
    unittest.main()
