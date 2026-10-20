#!/usr/bin/env python3
"""Summarize one reset-free low-rate directional PHY experiment. No hardware I/O."""
import argparse
import json
from pathlib import Path
import re


def analyze(tx, rx, source, minimum=100):
    source = source.upper()
    attempts = [int(x) for x in re.findall(r'PHY TX src='+source+r' seq=(\d+)', tx)]
    completed = [int(x) for x in re.findall(r'PHY DONE src='+source+r' seq=(\d+)', tx)]
    received = [int(x) for x in re.findall(r'PHY RX src='+source+r' seq=(\d+) valid=1', rx)]
    if not attempts or len(attempts) != len(set(attempts)):
        raise ValueError('Missing TX lines or repeated sequence/reset; split logs by boot')
    if any(((b-a)&0xffffffff) != 1 for a,b in zip(attempts,attempts[1:])):
        raise ValueError('TX log has missing records/reset; preserve raw serial and retry')
    sent = set(attempts)
    seen = [s for s in received if s in sent]
    summary = dict(source=source, tx_attempts=len(attempts), tx_completions=len(set(completed)&sent),
                   valid_rx_frames=len(seen), unique_received=len(set(seen)),
                   duplicate_rx_records=len(seen)-len(set(seen)), missing_sequences=sorted(sent-set(seen)),
                   rx_outside_tx_window=sum(s not in sent for s in received))
    # Gap counts only in the captured TX window. Late arrivals can fill these.
    summary['sequence_gaps']=sum(max(0,((b-a)&0xffffffff)-1) for a,b in zip(seen,seen[1:])
                                 if 0<((b-a)&0xffffffff)<0x80000000)
    for label, log in [('tx',tx),('rx',rx)]:
        lines = re.findall(r'PHY STAT ([^\r\n]*)',log)
        summary[label+'_last_counters'] = dict(re.findall(r'(\w+)=(\d+)',lines[-1])) if lines else None
    errs = re.findall(r'PHY ERR ([^\r\n]*)',rx)
    summary['rx_last_error_detail'] = errs[-1] if errs else 'none logged; DW1000 reports aggregate errors only'
    summary['minimum_completed_met']=summary['tx_completions']>=minimum
    summary['reception_demonstrated']=summary['unique_received']>0
    summary['capture_warning']='USB logging is best effort; missing serial records are not proven RF loss'
    return summary


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('tx_log',type=Path); p.add_argument('rx_log',type=Path)
    p.add_argument('--source',choices=['84','86'],required=True); p.add_argument('--minimum',type=int,default=100)
    a=p.parse_args()
    try:
        result=analyze(a.tx_log.read_text(errors='replace'),a.rx_log.read_text(errors='replace'),a.source,a.minimum)
    except ValueError as e:
        p.error(str(e))
    print(json.dumps(result,indent=2))
    return 0 if result['minimum_completed_met'] and result['reception_demonstrated'] else 1


if __name__=='__main__':
    raise SystemExit(main())
