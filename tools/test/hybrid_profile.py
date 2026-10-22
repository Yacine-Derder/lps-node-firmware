#!/usr/bin/env python3
"""Compile the actual custom-profile guard; no hardware or repository mutation."""
from pathlib import Path
import subprocess
ROOT=Path(__file__).resolve().parents[2]
base=['cc','-x','c','-fsyntax-only','-DSTM32F072xB','-DHSI48_VALUE=((uint32_t)48000000)']
base+=['-I'+p for p in ['inc','inc/f0','lib/freertos/inc','hal/stm32f0xx/Inc','vendor/libdw1000/inc']]
for enabled in (0,1):subprocess.run(base+[f'-DMUL_HYBRID_CH5={enabled}','inc/phy_interop_dw.h'],cwd=ROOT,check=True)
for flags in (['-DMUL_HYBRID_CH5=2'],['-DMUL_HYBRID_CH5=1','-DMUL_TWR_ROLE=2'],['-DMUL_HYBRID_CH5=1','-DPHY_INTEROP_ROLE=1'],['-DMUL_HYBRID_CH5=1','-DMUL_DIAG_ROLE=2']):
 r=subprocess.run(base+flags+['inc/phy_interop_dw.h'],cwd=ROOT,capture_output=True,text=True)
 assert r.returncode and 'MUL_HYBRID_CH5 is 0/1' in r.stderr,r.stderr
print('Custom channel5 profile: valid builds and conflicting/invalid role rejection passed')
