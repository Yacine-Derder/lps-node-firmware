#!/usr/bin/env python3
"""Count production RF5 codec Thumb instructions on an offline Cortex-M0.

Requires Unicorn, Capstone and pyelftools. Instruction counts are not cycle
simulation, SPI/IRQ timing or hardware WCET. Never opens a device.
"""
import argparse
from io import BytesIO
import json
from pathlib import Path
import subprocess
import tarfile
import tempfile

ROOT = Path(__file__).resolve().parents[2]
SOURCES = ('mul_plan_protocol', 'mul_ranging_plan', 'mul_multi_protocol',
           'mul_legacy_twr', 'mul_relation', 'mul_schedule', 'mul_common')


def build(directory, core):
    output = directory/'cost.elf'
    subprocess.run(['arm-none-eabi-gcc', '-Os', '-flto', '-mthumb', '-mcpu=cortex-m0',
                    '-ffunction-sections', '-fdata-sections', '-nostartfiles',
                    '--specs=nano.specs', '--specs=nosys.specs', '-I'+str(core),
                    str(ROOT/'tools/test/twr_cost.c'),
                    *(str(core/(s+'.c')) for s in SOURCES),
                    '-Wl,-Ttext=0x08000000,-Tdata=0x20000000,-e,_start,--gc-sections',
                    '-lm', '-lc', '-o', str(output)], check=True)
    return output


def measure(path):
    from capstone import Cs, CS_ARCH_ARM, CS_MODE_THUMB, CS_MODE_MCLASS
    from elftools.elf.elffile import ELFFile
    from unicorn import Uc, UC_ARCH_ARM, UC_MODE_THUMB, UC_MODE_MCLASS, UC_HOOK_BLOCK, UC_HOOK_CODE
    from unicorn.arm_const import UC_CPU_ARM_CORTEX_M0, UC_ARM_REG_SP, UC_ARM_REG_R0
    elf = ELFFile(BytesIO(path.read_bytes()))
    machine = Uc(UC_ARCH_ARM, UC_MODE_THUMB | UC_MODE_MCLASS)
    machine.ctl_set_cpu_model(UC_CPU_ARM_CORTEX_M0)
    machine.mem_map(0x08000000, 0x20000)
    machine.mem_map(0x20000000, 0x4000)
    for segment in elf.iter_segments():
        if segment['p_type'] == 'PT_LOAD':
            machine.mem_write(segment['p_vaddr'], segment.data())
    symbols = {s.name: s['st_value'] for s in elf.get_section_by_name('.symtab').iter_symbols()}
    active, counts, cache = [None], {}, {}
    decoder = Cs(CS_ARCH_ARM, CS_MODE_THUMB | CS_MODE_MCLASS)

    def marker(uc, address, size, data):
        if address == symbols['costBegin'] & ~1:
            active[0] = uc.reg_read(UC_ARM_REG_R0)
            counts[active[0]] = 0
        elif address == symbols['costEnd'] & ~1:
            active[0] = None
        else:
            uc.emu_stop()

    def block(uc, address, size, data):
        if active[0] is not None:
            if (address, size) not in cache:
                cache[address, size] = sum(1 for _ in decoder.disasm(bytes(uc.mem_read(address, size)), address))
            counts[active[0]] += cache[address, size]

    for name in ('costBegin', 'costEnd', 'costDone'):
        machine.hook_add(UC_HOOK_CODE, marker, begin=symbols[name] & ~1, end=symbols[name] & ~1)
    machine.hook_add(UC_HOOK_BLOCK, block)
    machine.reg_write(UC_ARM_REG_SP, 0x20004000)
    machine.emu_start(elf.header['e_entry'] | 1, 0, count=20000000)
    assert int.from_bytes(machine.mem_read(symbols['failure'], 4), 'little') == 0, 'codec fixture failed'
    assert all(k*10+stage in counts for k in range(4) for stage in range(3)), 'fixture incomplete'
    return {f'{3+k//2}alr_ff_{k%2}': {label: counts[k*10+stage]
            for stage, label in enumerate(('fingerprint', 'encode', 'decode'))} for k in range(4)}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--baseline', help='also measure a frozen Git core revision')
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='mul-twr-cost-') as temporary:
        directory = Path(temporary)
        result = {'current': measure(build(directory, ROOT/'portable/mul_core'))}
        if args.baseline:
            blob = subprocess.check_output(['git', 'archive', args.baseline, 'portable/mul_core'], cwd=ROOT)
            with tarfile.open(fileobj=BytesIO(blob)) as archive:
                archive.extractall(directory, filter='data')
            result['baseline'] = measure(build(directory, directory/'portable/mul_core'))
        print(json.dumps(result, indent=2))


if __name__ == '__main__':
    main()
