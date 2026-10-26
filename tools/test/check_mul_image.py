#!/usr/bin/env python3
"""Inspect the linked MUL-only Cortex-M0 image; no device access."""
import argparse
import json
from pathlib import Path
import subprocess


def inspect(path):
    symbols = {}
    for line in subprocess.check_output(['arm-none-eabi-nm', '-S', '--defined-only', str(path)], text=True).splitlines():
        fields = line.split()
        if len(fields) == 4:
            symbols[fields[3]] = int(fields[1], 16)
    removed = ('uwbTwrAnchorAlgorithm', 'uwbTwrTagAlgorithm', 'uwbSnifferAlgorithm',
               'uwbTdoa2Algorithm', 'uwbTdoa3Algorithm', 'uwbTdoa3ClockWorkspace',
               'modeState', 'productionTestsRun', 'cfgSetBinaryMode', 'cfgIsBinaryMode',
               'vApplicationGetTimerTaskMemory')
    assert not set(removed) & symbols.keys(), 'removed mode/factory state linked'
    assert not any(name.startswith(('lps25h', 'lppHandle')) for name in symbols), 'unused peripheral/LPP linked'
    for name in ('mulAlrAlgorithm', 'mulRlrAlgorithm', 'mulTwrOnEvent', 'mulClockObserveFitted'):
        assert name in symbols, f'missing MUL function: {name}'
    assert symbols.get('workspace') == 344, 'missing explicit bounded MUL clock workspace'
    # Preserve radio task/console/idle stack and USB queue allocations.
    for prefix, size in (('uwbStaticStack.', 1600), ('ucMainStack', 800),
                         ('uxIdleTaskStack.', 800), ('txqStorage', 512)):
        matches = [value for name, value in symbols.items() if name.startswith(prefix)]
        assert matches == [size], f'allocation changed: {prefix}: {matches}'
    sections = {}
    for line in subprocess.check_output(['arm-none-eabi-size', '-A', str(path)], text=True).splitlines():
        fields = line.split()
        if len(fields) == 3 and fields[0].startswith('.'):
            sections[fields[0]] = (int(fields[1]), int(fields[2]))
    flash = sum(size for name, (size, _) in sections.items()
                if name in ('.isr_vector', '.text', '.ARM.exidx', '.init_array', '.fini_array', '.data'))
    ram = sum(size for name, (size, _) in sections.items() if name in ('.data', '.bss', '._heap', '._stack'))
    flash_origin = sections['.isr_vector'][1]
    assert flash_origin >= 0x08000000 and flash_origin + flash <= 0x08020000, 'flash overflow'
    for name in ('.data', '.bss', '._heap', '._stack'):
        size, address = sections[name]
        assert 0x20000000 <= address <= address + size <= 0x20004000, f'RAM overflow: {name}'
    return dict(flash_bytes=flash, flash_free_bytes=0x08020000-flash_origin-flash,
                ram_bytes_including_linker_stack=ram, ram_free_bytes=16384-ram,
                clock_workspace_bytes=symbols['workspace'], stock_modes_linked=False)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('elf', type=Path)
    print(json.dumps(inspect(parser.parse_args().elf), indent=2))
