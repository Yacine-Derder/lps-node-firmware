#!/usr/bin/env python3
"""Independent golden-vector checks of both repository codecs + LPS real driver.
No hardware access. Requires the existing native Unity runners (rake unit).
"""
import ctypes
import json
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
PEER = ROOT.parent / 'epuck-stm32'


def main():
    vectors = json.loads((ROOT / 'test/phy_interop_vectors.json').read_text())
    assert vectors == json.loads((PEER / 'tests/phy_interop/vectors.json').read_text())
    assert (ROOT / 'inc/phy_diag.h').read_bytes() == (PEER / 'Core/Inc/phy_diag.h').read_bytes()
    with tempfile.TemporaryDirectory(prefix='phy-check-') as directory:
        work = Path(directory)
        wrapper = work / 'codec.c'
        wrapper.write_text('#include "phy_diag.h"\n'
                           'void encode(unsigned char*p,unsigned char s,unsigned int n){phyDiagEncode(p,s,n);}\n'
                           'int decode(unsigned char*p,unsigned n,unsigned char*s,unsigned int*q){return phyDiagDecode(p,n,s,q);}\n')
        for include in [ROOT / 'inc', PEER / 'Core/Inc']:
            so = work / 'codec.so'
            subprocess.run(['gcc', '-shared', '-fPIC', '-std=c11', '-Wall', '-Werror', '-I'+str(include),
                            str(wrapper), '-o', str(so)], check=True)
            # Unique names prevent dynamic-loader caching of different builds.
            unique = work / ('lps.so' if include == ROOT / 'inc' else 'stm.so')
            so.rename(unique)
            lib = ctypes.CDLL(str(unique))
            for vector in vectors['vectors']:
                data = (ctypes.c_ubyte * 32)()
                lib.encode(data, ctypes.c_ubyte(vector['source']), ctypes.c_uint32(vector['sequence']))
                assert bytes(data).hex() == vector['hex']
                source, seq = ctypes.c_ubyte(99), ctypes.c_uint32(99)
                assert lib.decode(data, 32, ctypes.byref(source), ctypes.byref(seq)) == 1
                assert (source.value, seq.value) == (vector['source'], vector['sequence'])
                for length in range(65):
                    if length != 32:
                        assert lib.decode(data, length, ctypes.byref(source), ctypes.byref(seq)) == 0
                for index in range(32):
                    for bit in range(8):
                        data[index] ^= 1 << bit
                        assert lib.decode(data, 32, ctypes.byref(source), ctypes.byref(seq)) == 0
                        data[index] ^= 1 << bit
            print(f'{include}: 3 golden vectors, 768 single-bit corruptions and wrong lengths passed')
        includes = ['inc', 'inc/f0', 'test', 'vendor/unity/src', 'lib/freertos/inc',
                    'hal/stm32f0xx/Inc', 'vendor/libdw1000/inc']
        for role in [1, 2]:
            command = ['gcc', '-std=c99', '-O1', '-g', '-Wall', '-Wextra', '-Werror',
                       '-Wno-unused-parameter', '-Wno-sign-compare', '-DUNITY_SUPPORT_64',
                       '-DSTM32F072xB', '-DHSI48_VALUE=((uint32_t)48000000)', f'-DPHY_INTEROP_ROLE={role}',
                       '-fsanitize=address,undefined', '-fno-pie', '-no-pie']
            command += ['-I' + i for i in includes]
            command += ['test/TestPhyRadio.c', 'generated-test/build/TestPhyRadio_Runner.c',
                        'src/mul_role_state.c', 'vendor/unity/src/unity.c', '-lm', '-o', str(work / 'radio')]
            subprocess.run(command, cwd=ROOT, check=True)
            subprocess.run([str(work / 'radio')], cwd=ROOT, check=True)
        for role in ['-1', '3']:
            result = subprocess.run(['gcc', '-x', 'c', '-fsyntax-only', f'-DPHY_INTEROP_ROLE={role}',
                                     'inc/phy_diag.h'], cwd=ROOT, capture_output=True)
            assert result.returncode != 0
    print('Cross-repository codec/profile contract and both DW1000 role sanitizer tests passed')


if __name__ == '__main__':
    main()
