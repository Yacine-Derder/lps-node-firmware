#!/usr/bin/env python3
"""Compare production C with the original experiment's NumPy model, read-only.

No parameter selection or held-out tuning. Temporary outputs only. Requires NumPy
already used by the experiment analysis. Optional original prototype/recording.
"""
import argparse
import ast
import ctypes as C
import json
import math
from pathlib import Path
import subprocess
import tempfile
import re
import numpy as np
ROOT=Path(__file__).resolve().parents[2]
DT=63897600000;B=10**9;MASK=(1<<40)-1

def compare(recording,prototype):
    # Load ONLY the original function; importing the analysis module would run
    # tuning and rewrite outputs. We explicitly do neither.
    tree=ast.parse(prototype.read_text())
    node=next(n for n in tree.body if isinstance(n,ast.FunctionDef) and n.name=='kalman')
    env=dict(np=np,math=math,DT=DT,B=B)
    exec(compile(ast.Module(body=[node],type_ignores=[]),str(prototype),'exec'),env)
    points=[];observations=[];last=None;phase=0;epoch=None
    if recording.suffix=='.log':
        # REF_RX records contain real TX/RX markers but no MCU uptime. Infer
        # coarse age ONLY from the declared 1Hz sequence cadence; KF dt still
        # comes from the hardware reference TX difference. Not a recovery test.
        source=[];last_seq=None;coarse=10000
        for seq,tx,rx in re.findall(r'REF_RX src=0x84 seq=(\d+) tx=0x([0-9A-Fa-f]+) rx=0x([0-9A-Fa-f]+)',recording.read_text()):
            seq=int(seq)
            if last_seq is not None:coarse+=((seq-last_seq)&0xffffffff)*1000
            source.append(json.dumps(dict(type='tdoa_replay',seen_mask=1,capture_age_ms=[0],clock=dict(epoch=1),observations=[dict(valid=True,sequence=seq,tx_dtu=int(tx,16),raw_rx_dtu=int(rx,16),accepted_ms=coarse&0xffffffff)])))
            last_seq=seq
    else:source=recording.read_text().splitlines()
    for line in source:
        r=json.loads(line)
        if r.get('type')!='tdoa_replay' or not r['seen_mask']&1 or r['capture_age_ms'][0]>15:continue
        o=r['observations'][0]
        if not o['valid']:continue
        tx,rx=o['tx_dtu'],o['raw_rx_dtu'];now=o['accepted_ms'];key=(r['clock']['epoch'],o['sequence'],tx,rx)
        if key==last:continue
        reset=epoch!=key[0] or last is None or ((now-last_now)&0xffffffff)>=3000
        dt=0
        if not reset:
            dt=((tx-last[2]+(1<<39))&MASK)-(1<<39)
            dr=((rx-last[3]+(1<<39))&MASK)-(1<<39)
            reset=dt<=0 or dr<=0 or abs((dr-dt)*B/dt)>200000
        if reset:phase=0
        else:phase+=dr-dt
        points.append(dict(reset=reset,dt=dt/DT,z=phase))
        observations.append((o['sequence'],tx,rx,now));last=key;last_now=now;epoch=key[0]
    expected,_=env['kalman'](points,1024.,1.)
    with tempfile.TemporaryDirectory() as d:
        lib=Path(d)/'model.so'
        subprocess.run(['cc','-std=c11','-shared','-fPIC','-O2','-Iportable/mul_core','tools/test/clock_model_api.c','portable/mul_core/mul_clock_fit.c','portable/mul_core/mul_clock.c','portable/mul_core/mul_common.c','-lm','-o',str(lib)],cwd=ROOT,check=True)
        api=C.CDLL(str(lib));api.model_new.restype=C.c_void_p;api.model_free.argtypes=[C.c_void_p]
        api.model_step.argtypes=[C.c_void_p,C.c_uint32,C.c_uint64,C.c_uint64,C.c_uint32,C.POINTER(C.c_double)]
        ptr=api.model_new();assert ptr
        max_error=0;exact=0;applied=0
        try:
            for index,obs in enumerate(observations):
                out=(C.c_double*18)();api.model_step(ptr,*obs,out)
                max_error=max(max_error,abs(out[1]-expected[index,0]),abs(out[2]-expected[index,1]))
                if out[13]:
                    rate=expected[index,0]+expected[index,1]*.005
                    rounded=int(math.copysign(math.floor(abs(rate)+.5),rate))
                    exact+=out[12]==rounded;applied+=1
                assert np.isfinite(list(out)).all()
                assert np.linalg.eigvalsh(np.array(list(out)[3:12]).reshape(3,3)).min()>-1e-4
        finally:api.model_free(ptr)
    assert max_error<1e-6,(max_error,len(points))
    assert exact==applied and applied>10,(exact,applied)
    print(json.dumps(dict(observations=len(points),max_rate_drift_error=max_error,applied_rate_integer_matches=exact,applied=applied,R=1024,q=1,reference=str(prototype), coarse_age_source='inferred_1Hz_cadence_not_recorded_uptime' if recording.suffix=='.log' else 'recorded_receiver_uptime')))

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('recording',type=Path);p.add_argument('prototype',type=Path);a=p.parse_args();compare(a.recording,a.prototype)
