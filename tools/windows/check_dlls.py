#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Check the Windows package folder that tools/windows/package.sh leaves in BUILD/dist.

The folder must hold wox.exe, setup.exe (Phase 331: system DLLs only) and exactly three DLLs: xeen_sbpro.dll,
xeen_sc55.dll and xeen_mt32.dll (Phase 335: SDL2 is linked into wox.exe; there is no SDL2.dll). Each DLL imports
Windows system DLLs only (the GCC runtime is linked into the three synth DLLs) and exports exactly its interface;
the executable imports system DLLs only and exports nothing. Every export must point into an executable section: an
LTO link that drops a function named only in a .def file leaves an export table of placeholder addresses, which
this rejects.
Usage: check_dlls.py DIST"""
import argparse
import re
import subprocess
import sys
from pathlib import Path

OBJDUMP = 'x86_64-w64-mingw32-objdump'
SYSTEM = {'kernel32.dll', 'msvcrt.dll', 'user32.dll', 'gdi32.dll', 'winmm.dll', 'advapi32.dll',
          'ole32.dll', 'shell32.dll', 'imm32.dll', 'oleaut32.dll', 'version.dll', 'setupapi.dll'}
EXPORTS = {
    'xeen_mt32.dll': {'mt32emu_get_service_i'},
    'xeen_sbpro.dll': {'sbpro_get_api'},
    'xeen_sc55.dll': {'sc55_abi_version', 'sc55_open', 'sc55_open_progress', 'sc55_message',
                      'sc55_render', 'sc55_close'},
}
EXE = 'wox.exe'
SETUP = 'setup.exe'  # Phase 331: the settings tool (common controls, the themed choice buttons)
SETUP_SYSTEM = SYSTEM | {'comctl32.dll', 'uxtheme.dll', 'winhttp.dll'}  # Phase 335: the ROM downloads


def pe_info(path):
    """Return (imports {dll: [names]}, exports [names], export RVAs, image base, code ranges)."""
    text = subprocess.check_output([OBJDUMP, '-p', '-h', str(path)], text=True)
    imports, current, section = {}, None, None
    exports, rvas, code = [], [], []
    base = int(re.search(r'^ImageBase\s+([0-9a-fA-F]+)', text, re.M)[1], 16)
    lines = text.splitlines()
    for i, line in enumerate(lines):
        if 'DLL Name:' in line:
            current = line.split('DLL Name:')[1].strip().lower()
            imports[current] = []
            continue
        if current:
            m = re.match(r'\s+[0-9a-f]+\s+\d+\s+(\S+)$', line)
            if m:
                imports[current].append(m[1])
            elif not line.strip() or 'vma:' in line:
                pass
            else:
                current = None
        if line.startswith('Export Address Table --'):
            section = 'eat'
            continue
        if line.startswith('[Ordinal/Name Pointer] Table'):
            section = 'names'
            continue
        if section == 'eat':
            m = re.match(r'\s+\[\s*\d+\] \+base\[\s*\d+\] ([0-9a-f]+) Export RVA', line)
            if m:
                rvas.append(int(m[1], 16))
            elif line.strip():
                section = None
        elif section == 'names':
            m = re.match(r'\s+\[\s*\d+\] (\S+)', line)
            if m:
                exports.append(m[1])
            elif not line.strip():
                section = None
        m = re.match(r'\s+\d+ (\S+)\s+([0-9a-f]+)\s+([0-9a-f]+)\s', line)
        if m and i + 1 < len(lines) and 'CODE' in lines[i + 1]:
            start = int(m[3], 16)
            code.append((start, start + int(m[2], 16)))
    return imports, exports, rvas, base, code


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('dist', type=Path)
    a = p.parse_args()
    problems = []
    dlls = sorted(f.name for f in a.dist.iterdir() if f.suffix.lower() == '.dll')
    expected = sorted(EXPORTS)
    if dlls != expected:
        problems.append(f'DLLs {dlls}, expected {expected}')
    for exe in (EXE, SETUP):
        if not (a.dist / exe).is_file():
            problems.append(f'{exe} missing')
    if (a.dist / SETUP).is_file():
        foreign = sorted(set(pe_info(a.dist / SETUP)[0]) - SETUP_SYSTEM)
        if foreign:
            problems.append(f'{SETUP} imports {foreign}')
    for name in [EXE, *expected]:
        path = a.dist / name
        if not path.is_file():
            continue
        imports, exports, rvas, base, code = pe_info(path)
        foreign = sorted(set(imports) - SYSTEM)
        if foreign:
            problems.append(f'{name} imports {foreign}')
        if name == EXE:
            if exports:
                problems.append(f'{EXE} exports {sorted(exports)} (the static SDL2 must export nothing)')
            continue
        want = EXPORTS[name]
        if set(exports) != want:
            problems.append(f'{name} exports {sorted(set(exports) ^ want)} beyond/short of its interface')
        bad = [r for r in rvas if not any(lo <= base + r < hi for lo, hi in code)]
        if bad or len(set(rvas)) != len(rvas):
            problems.append(f'{name}: {len(bad)} exports outside code, {len(rvas) - len(set(rvas))} duplicates')
    for name in [EXE, SETUP, *expected]:
        path = a.dist / name
        if path.is_file():
            imports = pe_info(path)[0]
            print(f'{path.stat().st_size:9d} {name:18s} {" ".join(sorted(imports))}')
    for problem in problems:
        print('FAIL:', problem, file=sys.stderr)
    return 1 if problems else 0


if __name__ == '__main__':
    sys.exit(main())
