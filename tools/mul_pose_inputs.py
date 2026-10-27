#!/usr/bin/env python3
"""Generate seeded HOST-ONLY pose inputs and replay them through production C.

This tool opens files only. It is not a ROS/MCS bridge or a serial-device client.
Pristine truth and noisy observations must have separate output paths.
"""
import argparse
import json
import math
from pathlib import Path
import random
import struct
import subprocess
import tempfile
import zlib

ROOT=Path(__file__).resolve().parents[1]

def integer(value,low,high,name):
    if type(value) is not int or not low<=value<=high:
        raise ValueError(f'{name}: integer {low}..{high}')
    return value

def encode_pose(row):
    b=bytearray(80)
    b[:4]=bytes((1,integer(row['node'],0,254,'node'),integer(row['flags'],0,7,'flags'),0))
    fields=[integer(row[k],1 if k in ('clock','frame') else 0,0xffffffff,k)
            for k in ('clock','epoch','frame','sequence')]
    struct.pack_into('<IIIIQ',b,4,*fields,integer(row['source_us'],0,0xffffffffffffffff,'source_us'))
    if len(row['position_mm'])!=3 or len(row['covariance_mm2'])!=6:
        raise ValueError('position/covariance shape')
    struct.pack_into('<iii',b,28,*(integer(x,-1000000,1000000,'position_mm') for x in row['position_mm']))
    struct.pack_into('<iiiiii',b,40,*(integer(x,-1000000000,1000000000,'covariance_mm2') for x in row['covariance_mm2']))
    struct.pack_into('<HHH',b,64,integer(row['age_upper_ms'],0,65535,'age_upper_ms'),
                     integer(row['ttl_ms'],1,6000,'ttl_ms'),integer(row['time_uncertainty_ms'],0,65535,'time_uncertainty_ms'))
    struct.pack_into('<I',b,76,zlib.crc32(b[:76]))
    return bytes(b)

def generate(rows,*,seed,sigma_mm,rho=0,clock=1,epoch=1,frame=1,age_upper_ms=65535,
             ttl_ms=1000,time_uncertainty_ms=65535,local_start_ms=0):
    integer(seed,0,0xffffffff,'seed')
    integer(local_start_ms,0,0xffffffff,'local_start_ms')
    if len(sigma_mm)!=3 or any(not math.isfinite(v) or v<0 or v*v>1e9 for v in sigma_mm):
        raise ValueError('sigma_mm: three finite standard deviations0..sqrt(1e9)')
    if not math.isfinite(rho) or not -1<rho<1:
        raise ValueError('rho: finite and strictly between -1 and1')
    rngs={};states={};sequences={};times={};noisy=[];truth=[];first=None;previous=None
    for row in rows:
        if set(row)!={'node','source_us','x_mm','y_mm','z_mm'}:
            raise ValueError('truth rows require exactly node/source_us/x_mm/y_mm/z_mm')
        node=integer(row['node'],0,254,'node')
        stamp=integer(row['source_us'],0,0xffffffffffffffff,'source_us')
        if (node in times and stamp<=times[node]) or (previous is not None and stamp<previous):
            raise ValueError('truth timestamps must increase per robot and never decrease globally')
        position=[row[k] for k in ('x_mm','y_mm','z_mm')]
        if any(type(v) not in (int,float) or not math.isfinite(v) for v in position):
            raise ValueError('truth positions must be finite')
        if first is None:first=stamp
        if node not in rngs:rngs[node]=random.Random(seed*257+node)
        rng=rngs[node]
        noise=[rng.gauss(0,sigma) for sigma in sigma_mm]
        if node in states:noise=[rho*old+math.sqrt(1-rho*rho)*new for old,new in zip(states[node],noise)]
        states[node]=noise;previous=stamp;times[node]=stamp
        seq=sequences.get(node,0);sequences[node]=(seq+1)&0xffffffff
        diagonal=[math.ceil(v*v) for v in sigma_mm]
        pose=dict(type='pose',node=node,source_us=stamp,clock=clock,epoch=epoch,frame=frame,sequence=seq,
                  position_mm=[round(v+n) for v,n in zip(position,noise)],
                  covariance_mm2=[diagonal[0],0,0,diagonal[1],0,diagonal[2]],flags=7,
                  age_upper_ms=age_upper_ms,ttl_ms=ttl_ms,time_uncertainty_ms=time_uncertainty_ms,
                  received_ms=(local_start_ms+(stamp-first)//1000)&0xffffffff,
                  input_source='synthetic_host',noise=dict(seed=seed,sigma_mm=list(sigma_mm),rho_per_sample=rho,
                  covariance_scope='configured_gaussian_before_mm_quantization',quantization_bound_mm=0.5))
        pose['payload_hex']=encode_pose(pose).hex().upper()
        noisy.append(pose);truth.append(dict(row))
    return noisy,truth

def replay(rows,reference,follower,max_skew_ms=0):
    integer(reference,0,254,'reference');integer(follower,0,254,'follower')
    integer(max_skew_ms,0,1000,'max_skew_ms')
    if reference==follower:raise ValueError('reference and follower must differ')
    payload=bytearray()
    for row in rows:
        encoded=encode_pose(row)
        if row['payload_hex']!=encoded.hex().upper():raise ValueError('pose fields disagree with preserved raw payload')
        payload+=struct.pack('<I',integer(row['received_ms'],0,0xffffffff,'received_ms'))+encoded
    with tempfile.TemporaryDirectory(prefix='mul-pose-replay-') as work:
        core=ROOT/'portable/mul_core';exe=Path(work)/'replay'
        subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-O2','-I'+str(core),
                        str(ROOT/'tools/test/pose_replay.c'),str(core/'mul_pose.c'),
                        str(core/'mul_schedule.c'),str(core/'mul_common.c'),'-lm','-o',str(exe)],check=True)
        result=subprocess.run([str(exe),str(reference),str(follower),str(max_skew_ms)],input=payload,capture_output=True,check=True)
        return [json.loads(line) for line in result.stdout.splitlines()]

def read(path):
    return [json.loads(line) for line in path.read_text().splitlines() if line.strip()]

def write(path,rows):
    path.write_text(''.join(json.dumps(row,separators=(',',':'),allow_nan=False)+'\n' for row in rows))

def distinct(*paths):
    resolved=[p.resolve() for p in paths]
    if len(set(resolved))!=len(resolved) or any(a.exists() and b.exists() and a.samefile(b) for i,a in enumerate(paths) for b in paths[i+1:]):
        raise ValueError('input, truth, noisy and command/output paths must be distinct')

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    sub=parser.add_subparsers(dest='action',required=True)
    gen=sub.add_parser('generate',help='Create file artifacts; never send commands to a device')
    for name in ('input','truth','noisy','commands'):gen.add_argument('--'+name,type=Path,required=True)
    gen.add_argument('--seed',type=int,required=True)
    gen.add_argument('--sigma-mm',type=float,nargs=3,required=True)
    gen.add_argument('--rho',type=float,default=0)
    gen.add_argument('--clock',type=int,default=1);gen.add_argument('--epoch',type=int,default=1);gen.add_argument('--frame',type=int,default=1)
    gen.add_argument('--age-upper-ms',type=int,default=65535,help='Simulation acceptance-age upper bound;65535 UNKNOWN')
    gen.add_argument('--ttl-ms',type=int,default=1000)
    gen.add_argument('--time-uncertainty-ms',type=int,default=65535)
    gen.add_argument('--local-start-ms',type=int,default=0)
    run=sub.add_parser('replay',help='Replay files through production portable C geometry; no devices')
    run.add_argument('--input',type=Path,required=True);run.add_argument('--output',type=Path,required=True)
    run.add_argument('--reference',type=int,required=True);run.add_argument('--follower',type=int,required=True)
    run.add_argument('--max-skew-ms',type=int,default=0)
    args=parser.parse_args()
    try:
        if args.action=='generate':
            distinct(args.input,args.truth,args.noisy,args.commands)
            noisy,truth=generate(read(args.input),seed=args.seed,sigma_mm=args.sigma_mm,rho=args.rho,
                                 clock=args.clock,epoch=args.epoch,frame=args.frame,age_upper_ms=args.age_upper_ms,
                                 ttl_ms=args.ttl_ms,time_uncertainty_ms=args.time_uncertainty_ms,local_start_ms=args.local_start_ms)
            write(args.truth,truth);write(args.noisy,noisy)
            args.commands.write_text(f':pose begin {args.clock} {args.epoch} {args.frame} 0\n'+''.join(':pose put '+p['payload_hex']+'\n' for p in noisy))
        else:
            distinct(args.input,args.output)
            write(args.output,replay(read(args.input),args.reference,args.follower,args.max_skew_ms))
    except (ValueError,KeyError,TypeError,subprocess.CalledProcessError) as error:
        parser.exit(2,f'{error}\n')

if __name__=='__main__':main()
