#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Bundle the release archives of WoX from the packaged programs.

Writes into OUT (default build/release):

  wox-VERSION-windows-x64.zip      wox.exe, setup.exe, xeen_*.dll
  wox-VERSION-linux-x64.tar.gz     wox, setup, libxeen_*.so.1, libmt32emu.so.2

at the top of the archive (no enclosing folder: unpack it into the folder you want), and beside them the folder
extras/ with everything else:

  extras/README.md, extras/docs/         the user documentation (and docs/images/, the README's pictures)
  extras/licenses/                       LICENSE (GPL-3.0), THIRD_PARTY_NOTICES.md and the third-party licence texts
  extras/source/wox-VERSION-src.tar.gz   the complete source of this program (the GPL's "corresponding source")
  extras/source/third-party/             the exact source archives of SDL 2, Munt, ymfm and Nuked-SC55 used to build
  extras/source/SOURCE.txt               what these are and how to rebuild

The programs come from tools/windows/package.sh and tools/linux/package.sh (--build runs them first). The
third-party sources come from DEPS (tools/deps/fetch_deps.sh); their SHA-256 / commits are checked again here.
The source archive never contains generated program data, game files, ROMs or build output: the tree's files are
taken from `git ls-files` when the tree is the top of a Git checkout, otherwise by walking it with the exclusions below, and
every name is checked against the forbidden patterns before anything is written.

Usage: tools/release/make_release.py [--build] [--only windows|linux] [--out DIR] [--deps DIR]
                                     [--linux-dist DIR] [--windows-dist DIR]
"""
import argparse
import fnmatch
import hashlib
import io
import os
import re
import subprocess
import sys
import tarfile
import time
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]

WINDOWS_FILES = ['wox.exe', 'setup.exe', 'xeen_sbpro.dll', 'xeen_sc55.dll', 'xeen_mt32.dll']
LINUX_FILES = ['wox', 'setup', 'libxeen_sbpro.so.1', 'libxeen_sc55.so.1', 'libmt32emu.so.2']
USER_DOCS = ['docs/CONFIGURATION.md', 'docs/VERSIONING.md', 'docs/BUILDING.md']

# The third-party sources (tools/deps/fetch_deps.sh): archive name -> (where, SHA-256 or git commit)
THIRD_PARTY = {
    'SDL2-2.30.11.tar.gz': ('downloads/SDL2-2.30.11.tar.gz',
                            '8b8d4aef2038533da814965220f88f77d60dfa0f32685f80ead65e501337da7f'),
    'munt-libmt32emu_2_8_3.tar.gz': ('downloads/munt-libmt32emu_2_8_3.tar.gz',
                                     '81f8c462f46bc8901618762ae34cf9de93894ff81f41db73c79472fa3baef875'),
    'ymfm-81aec25ccbb98f4873a255f7551ac4dadac59b4a.tar.gz': ('git:ymfm-src', '81aec25ccbb98f4873a255f7551ac4dadac59b4a'),
    'Nuked-SC55-66d12727cc43000d38094aaf2f76dc30cc93f448.tar.gz': ('git:nuked-sc55-src',
                                                                    '66d12727cc43000d38094aaf2f76dc30cc93f448'),
}

# Never in a source archive (when the tree is walked rather than taken from git)
EXCLUDE_DIRS = {'.git', 'build', 'deps', 'dist', '__pycache__', '.vscode', '.idea'}
FORBIDDEN = ['*.dat', '*.cc', '*.cur', '*.wox', '*.rom', '*.bin', '*.cue', '*.iso', '*.gog', '*.ogg', '*.exe',
             '*.dll', '*.so', '*.so.*', '*.zip', '*.pyc', 'xeen.cfg', 'wox.cfg', 'wox.log']


def version():
    text = (ROOT / 'versions.cmake').read_text()
    m = re.search(r'set\(WOX_VERSION "([0-9.]+)"\)', text)
    if not m:
        sys.exit('versions.cmake: WOX_VERSION not found')
    return m[1]


def sha256(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def in_git():
    r = subprocess.run(['git', '-C', str(ROOT), 'rev-parse', '--show-toplevel'], capture_output=True, text=True)
    return r.returncode == 0 and Path(r.stdout.strip()).resolve() == ROOT


def source_files():
    git = in_git()
    if git:
        out = subprocess.run(['git', '-C', str(ROOT), 'ls-files', '-z'], capture_output=True, check=True).stdout
        names = [n for n in out.decode().split('\0') if n]
    else:
        names = []
        for dirpath, dirnames, filenames in os.walk(ROOT):
            rel = Path(dirpath).relative_to(ROOT)
            dirnames[:] = sorted(d for d in dirnames if d not in EXCLUDE_DIRS
                                 and not (rel == Path('tools/data') and d == 'meta'))
            for f in sorted(filenames):
                names.append(str(rel / f) if str(rel) != '.' else f)
    keep, problems = [], []
    for n in sorted(names):
        p = ROOT / n
        if n.startswith('src/data/') and n != 'src/data/README.md':
            if git:
                problems.append(f'generated program data is tracked by git: {n}')
            continue                                   # generated from the user's XEEN.DAT: never distributed
        if any(fnmatch.fnmatch(Path(n).name.lower(), pat) for pat in FORBIDDEN):
            problems.append(f'forbidden file in the source tree: {n}')
            continue
        if p.is_symlink() or not p.is_file():
            problems.append(f'not a regular file: {n}')
            continue
        keep.append(n)
    if problems:
        sys.exit('refusing to build the source archive:\n  ' + '\n  '.join(problems))
    return keep


def tar_bytes(entries, prefix):
    """entries: [(name in the archive, bytes, mode)] -> a .tar.gz in memory, sorted, owner-free."""
    buf = io.BytesIO()
    now = int(os.environ.get('SOURCE_DATE_EPOCH', time.time()))
    with tarfile.open(fileobj=buf, mode='w:gz', format=tarfile.PAX_FORMAT) as tar:
        for name, data, mode in sorted(entries):
            info = tarfile.TarInfo(f'{prefix}/{name}' if prefix else name)
            info.size, info.mode, info.mtime = len(data), mode, now
            info.uid = info.gid = 0
            info.uname = info.gname = ''
            tar.addfile(info, io.BytesIO(data))
    return buf.getvalue()


def file_entry(name, path):
    mode = 0o755 if os.access(path, os.X_OK) else 0o644
    return (name, Path(path).read_bytes(), mode)


def third_party(deps):
    out = []
    for name, (where, pin) in THIRD_PARTY.items():
        if where.startswith('git:'):
            repo = deps / where[4:]
            head = subprocess.run(['git', '-C', str(repo), 'rev-parse', 'HEAD'], capture_output=True,
                                  text=True).stdout.strip()
            if head != pin:
                sys.exit(f'{repo}: not at {pin} (run tools/deps/fetch_deps.sh)')
            data = subprocess.run(['git', '-C', str(repo), 'archive', '--format=tar.gz',
                                   f'--prefix={name[:-len(".tar.gz")]}/', pin], capture_output=True, check=True).stdout
        else:
            path = deps / where
            if not path.is_file() or sha256(path) != pin:
                sys.exit(f'{path}: missing or not the pinned archive (run tools/deps/fetch_deps.sh)')
            data = path.read_bytes()
        out.append((f'source/third-party/{name}', data, 0o644))
    return out


SOURCE_TXT = """WoX {v} - source code

wox-{v}-src.tar.gz
    The complete source of WoX {v} and setup {sv} as released: the C sources, the build files (CMake), the scripts
    that fetch the dependencies and build these packages (tools/), the tests and the documentation. It is the
    "corresponding source" the GNU General Public License requires for the programs in this archive.
    It does NOT contain the program data generated from the original game's XEEN.DAT, any game file or ROM:
    to build, you need your own copy of the game (see src/data/README.md and docs/BUILDING.md inside).

third-party/
    The exact sources of the libraries built into this package, unmodified, as downloaded by
    tools/deps/fetch_deps.sh (licences: ../licenses/THIRD_PARTY_NOTICES.md):
{tp}

To rebuild: unpack wox-{v}-src.tar.gz, follow docs/BUILDING.md; the third-party archives can be placed where
tools/deps/fetch_deps.sh expects them (deps/downloads/ for the two release archives) instead of downloading them.
"""


def extras(deps, setup_version):
    entries = [file_entry('README.md', ROOT / 'README.md')]
    entries += [file_entry(d, ROOT / d) for d in USER_DOCS]
    entries += [file_entry(f'docs/images/{p.name}', p) for p in sorted((ROOT / 'docs' / 'images').glob('*'))
                if p.is_file()]  # the README's pictures, at the same relative path
    entries += [file_entry('licenses/LICENSE', ROOT / 'LICENSE'),
                file_entry('licenses/THIRD_PARTY_NOTICES.md', ROOT / 'THIRD_PARTY_NOTICES.md')]
    entries += [file_entry(f'licenses/{p.name}', p) for p in sorted((ROOT / 'LICENSES').iterdir())]
    v = version()
    src = [file_entry(n, ROOT / n) for n in source_files()]
    entries.append((f'source/wox-{v}-src.tar.gz', tar_bytes(src, f'wox-{v}-src'), 0o644))
    tp = third_party(deps)
    entries += tp
    listing = '\n'.join(f'      {Path(n).name}' for n, _, _ in tp)
    entries.append(('source/SOURCE.txt', SOURCE_TXT.format(v=v, sv=setup_version, tp=listing).encode(), 0o644))
    return [(f'extras/{n}', d, m) for n, d, m in entries], len(src)


def programs(dist, names):
    missing = [n for n in names if not (dist / n).is_file()]
    if missing:
        sys.exit(f'{dist}: missing {", ".join(missing)} (run the package script, or --build)')
    for n in names:  # a CI build with the placeholder program data (XEEN_PROGRAM_DATA_STUB) must never be released
        if b'placeholder: not the program data' in (dist / n).read_bytes():
            sys.exit(f'{dist / n}: built with XEEN_PROGRAM_DATA_STUB (placeholder program data): not releasable')
    return [file_entry(n, dist / n) for n in names]


def write_zip(path, prefix, entries):
    with zipfile.ZipFile(path, 'w', zipfile.ZIP_DEFLATED, compresslevel=9) as z:
        for name, data, mode in sorted(entries):
            info = zipfile.ZipInfo(f'{prefix}/{name}' if prefix else name, date_time=time.localtime()[:6])
            info.external_attr = (0o100000 | mode) << 16
            info.compress_type = zipfile.ZIP_DEFLATED
            z.writestr(info, data)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--build', action='store_true', help='run the package scripts first')
    ap.add_argument('--only', choices=('windows', 'linux'))
    ap.add_argument('--out', type=Path, default=ROOT / 'build' / 'release')
    ap.add_argument('--deps', type=Path, default=Path(os.environ.get('XEEN_AUDIO_DEPS', ROOT / 'deps')))
    ap.add_argument('--linux-dist', type=Path, default=ROOT / 'build' / 'linux-package' / 'dist')
    ap.add_argument('--windows-dist', type=Path, default=ROOT / 'build' / 'windows-package' / 'dist')
    a = ap.parse_args()
    targets = [a.only] if a.only else ['windows', 'linux']
    if a.build:
        for t in targets:
            dist = a.windows_dist if t == 'windows' else a.linux_dist
            subprocess.run([str(ROOT / 'tools' / t / 'package.sh'), str(dist.parent)], check=True,
                           env={**os.environ, 'XEEN_AUDIO_DEPS': str(a.deps)})
    v = version()
    sv = re.search(r'set\(SETUP_VERSION "([0-9.]+)"\)', (ROOT / 'versions.cmake').read_text())[1]
    extra, nsrc = extras(a.deps, sv)
    a.out.mkdir(parents=True, exist_ok=True)
    for t in targets:
        prefix = f'wox-{v}-{t}-x64'
        if t == 'windows':
            path = a.out / f'{prefix}.zip'
            write_zip(path, None, programs(a.windows_dist, WINDOWS_FILES) + extra)
        else:
            path = a.out / f'{prefix}.tar.gz'
            path.write_bytes(tar_bytes(programs(a.linux_dist, LINUX_FILES) + extra, None))
        print(f'{path} ({path.stat().st_size:,} bytes; source archive: {nsrc} files)')


if __name__ == '__main__':
    main()
