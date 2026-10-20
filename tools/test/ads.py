#!/usr/bin/env python3
"""Host sanitizer tests against the actual DW1000 register driver; no hardware."""
from pathlib import Path
import subprocess,tempfile
root=Path(__file__).resolve().parents[2]
with tempfile.TemporaryDirectory(prefix='mul-ads-dw-') as work:
 for role,trace in ((1,0),(1,1),(2,0),(2,1)):
  cmd=['cc','-std=c11','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-Wno-sign-compare',
       '-g','-fsanitize=address,undefined','-fno-pie','-no-pie','-DSTM32F072xB',
       '-DHSI48_VALUE=((uint32_t)48000000)',f'-DMUL_ADS_TRACE_TIMES={trace}',f'-DMUL_TWR_ROLE={role}','-DMUL_TWR_SESSION=123ULL']
  cmd+=['-I'+p for p in ('inc','inc/f0','lib/freertos/inc','hal/stm32f0xx/Inc','vendor/libdw1000/inc')]
  cmd+=['tools/test/test_ads_dw.c','src/mul_ads.c','src/mul_core.c','src/mul_common.c','src/mul_role_state.c','-lm','-o',work+'/test']
  subprocess.run(cmd,cwd=root,check=True)
  output=subprocess.check_output([work+'/test'],text=True)
  if role==1:
   assert 'range_valid=0' in output and 'cause=6 stage=6' in output
   assert ('TIMES session=' in output)==bool(trace)
  print(output,end='')
 for flags in [['-DMUL_TWR_ROLE=3'],['-DMUL_TWR_ROLE=1'],['-DMUL_TWR_ROLE=2','-DMUL_DIAG_ROLE=1'],['-DMUL_TWR_ROLE=2','-DPHY_INTEROP_ROLE=1']]:
  r=subprocess.run(['cc','-x','c','-fsyntax-only',*flags,'portable/mul_core/mul_diag_config.h'],cwd=root,capture_output=True)
  assert r.returncode!=0,flags
