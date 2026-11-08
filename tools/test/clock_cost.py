#!/usr/bin/env python3
"""Compare actual -Os Cortex-M0 clock code against a frozen Git baseline.

Optional offline tool: requires arm-none-eabi-gcc, Unicorn, Capstone, pyelftools.
Counts executed Thumb instructions and software-double helper calls; this is
NOT a cycle simulator or hardware WCET. Never opens hardware/peripheral ports.
"""
import argparse
from collections import Counter
from io import BytesIO
import json
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
BASELINE = 'de20a760636c8df1902a5d938b84247039f19d7e'


def build(directory, name, source, period, motion_guard=False):
    out = directory / (name + '.elf')
    subprocess.run(['arm-none-eabi-gcc', '-Os', '-g', '-mthumb', '-mcpu=cortex-m0',
                    '-ffunction-sections', '-fdata-sections', '-nostartfiles',
                    '--specs=nano.specs', '--specs=nosys.specs',
                    '-DMUL_PROP_MAX_AGE_MS=6000', '-DMUL_RX_MOTION_GUARD=1', '-DCLOCK_COST_MOTION_GUARD='+str(int(motion_guard)), '-DCLOCK_COST_PERIOD_MS='+str(period), '-DCLOCK_COST_DEFERRED='+('1' if name=='current' else '0'), '-Iportable/mul_core',
                    'tools/test/clock_cost.c', str(source),
                    'portable/mul_core/mul_clock.c', 'portable/mul_core/mul_common.c',
                    '-Wl,-Ttext=0x08000000,-Tdata=0x20000000,-e,_start,--gc-sections',
                    '-lm', '-lc', '-o', str(out)], cwd=ROOT, check=True)
    return out


def measure(path):
    from capstone import Cs, CS_ARCH_ARM, CS_MODE_THUMB, CS_MODE_MCLASS
    from elftools.elf.elffile import ELFFile
    from unicorn import Uc, UC_ARCH_ARM, UC_MODE_THUMB, UC_MODE_MCLASS, UC_HOOK_BLOCK, UC_HOOK_CODE
    from unicorn.arm_const import UC_CPU_ARM_CORTEX_M0, UC_ARM_REG_SP, UC_ARM_REG_R0, UC_ARM_REG_LR
    elf = ELFFile(BytesIO(path.read_bytes()))
    machine = Uc(UC_ARCH_ARM, UC_MODE_THUMB | UC_MODE_MCLASS)
    machine.ctl_set_cpu_model(UC_CPU_ARM_CORTEX_M0)
    machine.mem_map(0x08000000, 0x20000)
    machine.mem_map(0x20000000, 0x4000)
    for segment in elf.iter_segments():
        if segment['p_type'] == 'PT_LOAD':
            machine.mem_write(segment['p_vaddr'], segment.data())
    symbols = {s.name: s['st_value'] & ~1 for s in elf.get_section_by_name('.symtab').iter_symbols()
               if s['st_info']['type'] == 'STT_FUNC'}
    helpers = {addr: name for name, addr in symbols.items() if name.startswith('__aeabi_d')}
    scopes = {addr: ('prediction' if name.startswith('predict') else
                     'measurement_update' if name == 'update' else
                     'numerical_checks' if name == 'healthy' else 'current_prior')
              for name, addr in symbols.items()
              if name.startswith('predict') or name in ('update', 'healthy', 'mulClockFitPrior')}
    stack = []
    decoder = Cs(CS_ARCH_ARM, CS_MODE_THUMB | CS_MODE_MCLASS)
    lengths = {}
    active, results = [None], []

    def block(uc, address, size, _):
        if active[0] is None:
            return
        key = address, size
        if key not in lengths:
            lengths[key] = sum(1 for _ in decoder.disasm(bytes(uc.mem_read(address, size)), address))
        while stack and address == stack[-1][1]:
            stack.pop()
        if address in scopes:
            stack.append((scopes[address], uc.reg_read(UC_ARM_REG_LR) & ~1))
        active[0]['instructions'] += lengths[key]
        active[0]['by_stage'][stack[-1][0] if stack else 'bookkeeping_and_publication'] += lengths[key]
        if address in helpers:
            active[0]['double_helpers'][helpers[address]] += 1

    def marker(uc, address, size, _):
        if address == symbols['clockCostBegin']:
            stack.clear()
            active[0] = dict(kind=uc.reg_read(UC_ARM_REG_R0), instructions=0, double_helpers=Counter(), by_stage=Counter())
        elif address == symbols['clockCostEnd']:
            results.append(active[0]);active[0] = None
        elif address == symbols['clockCostDone']:
            uc.emu_stop()

    machine.hook_add(UC_HOOK_BLOCK, block)
    for name in ('clockCostBegin', 'clockCostEnd', 'clockCostDone'):
        machine.hook_add(UC_HOOK_CODE, marker, begin=symbols[name], end=symbols[name])
    machine.reg_write(UC_ARM_REG_SP, 0x20004000)
    machine.emu_start(elf.header['e_entry'] | 1, 0, count=20000000)
    assert sum(r['kind']!=4 for r in results) == 16, 'fixture failed to complete within bounded instruction limit'
    failure = next(s for s in elf.get_section_by_name('.symtab').iter_symbols() if s.name == 'clockCostFailure')
    assert int.from_bytes(machine.mem_read(failure['st_value'], 4), 'little') == 0, 'fixture did not sustain regular valid Kalman updates'
    objects = {s.name: (s['st_value'], s['st_size']) for s in elf.get_section_by_name('.symtab').iter_symbols()
               if s.name in ('clockState', 'fit')}
    snapshot = {name: bytes(machine.mem_read(address, size)).hex()
                for name, (address, size) in objects.items()}
    assert set(snapshot) == {'clockState', 'fit'}
    return results, snapshot


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--baseline', default=BASELINE)
    parser.add_argument('--period', type=int, choices=(200, 1000), default=1000)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='mul-clock-cost-') as temporary:
        work = Path(temporary)
        old = work / 'baseline.c'
        old.write_bytes(subprocess.check_output(['git', 'show', args.baseline + ':portable/mul_core/mul_clock_fit.c'], cwd=ROOT))
        measured = {name: measure(build(work, name, source, args.period)) for name, source in
                    (('baseline', old), ('current', ROOT/'portable/mul_core/mul_clock_fit.c'))}
        assert measured['baseline'][1] == measured['current'][1], 'Cortex-M0 posterior state changed'
        result = {name: pair[0] for name, pair in measured.items()}
        result['terminal_state_bit_identical'] = True
        regular = [max(r['instructions'] for r in result[name] if r['kind'] == 1)
                   for name in ('baseline', 'current')]
        assert regular[1] < regular[0] / 2, 'deferral must cut pre-TX instruction count by >2x'
        result['regular_max_instruction_ratio'] = regular[1] / regular[0]
        print(json.dumps(result, indent=2))


if __name__ == '__main__':
    main()
