#!/usr/bin/env python3
"""Cross-repository core pin/byte checks and real DW1000 diagnostic role tests.
Run after native Unity runners exist. No hardware or network access.
"""
import hashlib
import json
from pathlib import Path
import subprocess
import tempfile
root=Path(__file__).resolve().parents[2]
peer=root.parent/'epuck-stm32'
canonical=root/'portable/mul_core'
vendor=peer/'ThirdParty/mul_core'
pin=json.loads((vendor/'UPSTREAM.json').read_text())
# Check the actual GNU Make graph even in a fresh checkout with no objects.
plan=subprocess.run(['make','-qp'],cwd=root,capture_output=True,text=True)
assert plan.returncode in (0,1),plan.stderr
for obj in ['src/mul_common.o','src/mul_core.o','src/mul_diag_dw.o','src/uwb.o']:
    dependencies={item for line in plan.stdout.splitlines() if line.startswith(obj+':')
                  and 'CFLAGS' not in line for item in line.split(':',1)[1].split()}
    for source in canonical.glob('*.[ch]'):
        assert source.relative_to(root).as_posix() in dependencies,(obj,source)

subprocess.run(['python3',str(vendor/'verify.py')],check=True)
assert {p.relative_to(canonical).as_posix() for p in canonical.rglob('*') if p.is_file()}==set(pin['sha256'])
for relative,digest in pin['sha256'].items():
    assert (canonical/relative).read_bytes()==(vendor/relative).read_bytes()
    assert hashlib.sha256((canonical/relative).read_bytes()).hexdigest()==digest
for directory in [canonical,vendor]: subprocess.run(['sh',str(directory/'tests/run.sh')],check=True)
with tempfile.TemporaryDirectory(prefix='mul-core-cross-') as work:
    work=Path(work)
    includes=['inc','inc/f0','test','vendor/unity/src','lib/freertos/inc','hal/stm32f0xx/Inc','vendor/libdw1000/inc']
    for role in [1,2]:
        command=['gcc','-std=c11','-O1','-g','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-Wno-sign-compare',
                 '-DUNITY_SUPPORT_64','-DSTM32F072xB','-DHSI48_VALUE=((uint32_t)48000000)',f'-DMUL_DIAG_ROLE={role}',
                 '-fsanitize=address,undefined','-fno-pie','-no-pie']
        command+=['-I'+i for i in includes]
        command+=['test/TestMulDiagRadio.c','generated-test/build/TestMulDiagRadio_Runner.c','src/mul_core.c',
                  'src/mul_common.c','src/mul_role_state.c','vendor/unity/src/unity.c','-lm','-o',str(work/'radio')]
        subprocess.run(command,cwd=root,check=True);subprocess.run([str(work/'radio')],check=True)
    for defines in [['-DMUL_DIAG_ROLE=3'],['-DMUL_DIAG_ROLE=-1'],['-DMUL_DIAG_ROLE=1','-DPHY_INTEROP_ROLE=1'],['-DMUL_TIMING_TRACE=1']]:
        result=subprocess.run(['gcc','-x','c','-fsyntax-only',*defines,str(canonical/'mul_diag_config.h')],capture_output=True)
        assert result.returncode!=0
print('Canonical/vendor byte agreement, independent golden tests, both DW roles and configuration rejection passed')
