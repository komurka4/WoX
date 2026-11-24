"""Phase 338: the executables' version numbers (versions.cmake), wox --version and the versioned texts.

Usage: test_phase338_versions.py WOX BUILD_DIR [SETUP]
Environment (CMake sets it from versions.cmake): XEEN_WOX_VERSION, XEEN_SETUP_VERSION, XEEN_CMAKE.

Headless, no window (AGENTS §5): window sessions run from a private copy with DISPLAY and WAYLAND_DISPLAY removed.

  1. versions.cmake sets WOX_VERSION and SETUP_VERSION once each, to the values this build was configured with.
  2. The syntax check (cmake/xeen_version.cmake): accepted values and their Windows tuples; rejected values fail.
  3. The generated headers (BUILD_DIR/version/wox, BUILD_DIR/version/setup) each define only their own number.
  4. One source: no hand-written version text, version resource or version macro elsewhere in src/, include/,
     tests' C or the package scripts; both setup windows take their main title from setup_title.h.
  5. wox --version: exactly the version and a newline on stdout, nothing on stderr, status 0, from an empty working
     directory without data, wox.cfg or display, also among other (even invalid) arguments; no file is created
     anywhere (beside the executable, in the working directory, HOME or the XDG directories).
  6. --help names --version; a window session's wox.log starts with "session start: WoX (VERSION)" once.
  7. The binaries: wox holds its version texts and not setup's title; setup (when built) holds its title with
     SETUP_VERSION and not the game's log text.
"""
import os, re, shutil, subprocess, sys, tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
FAILS = []
LINE = re.compile(r'^\d{4}-\d\d-\d\d \d\d:\d\d:\d\d\.\d{3} INFO  (.*)$')


def check(cond, what):
    print(('ok   ' if cond else 'FAIL ') + what)
    if not cond:
        FAILS.append(what)


def tree(path):
    return sorted(str(p.relative_to(path)) for p in path.rglob('*'))


def run(exe, args, home, cwd, extra=None):
    env = {k: v for k, v in os.environ.items() if k not in ('DISPLAY', 'WAYLAND_DISPLAY')}
    env.update({'HOME': str(home), 'XDG_STATE_HOME': str(home / 'state'), 'XDG_CONFIG_HOME': str(home / 'config'),
                'XDG_DATA_HOME': str(home / 'data'), 'XDG_CACHE_HOME': str(home / 'cache')})
    env.update(extra or {})
    return subprocess.run([str(exe)] + args, capture_output=True, text=True, timeout=60, env=env, cwd=str(cwd))


def function_body(text, name):
    m = re.search(r'\nstatic void ' + name + r'\([^)]*\)\n\{\n(.*?)\n\}\n', text, re.S)
    return m[1] if m else ''


def main():
    if len(sys.argv) not in (3, 4):
        print(__doc__)
        return 2
    wox, build = Path(sys.argv[1]).resolve(), Path(sys.argv[2]).resolve()
    setup = Path(sys.argv[3]).resolve() if len(sys.argv) == 4 and sys.argv[3] else None
    wv, sv, cmake = os.environ.get('XEEN_WOX_VERSION'), os.environ.get('XEEN_SETUP_VERSION'), os.environ.get('XEEN_CMAKE', 'cmake')
    if not wv or not sv:
        print('XEEN_WOX_VERSION and XEEN_SETUP_VERSION must be set (CMake does)')
        return 2

    # 1. versions.cmake
    text = (ROOT / 'versions.cmake').read_text()
    sets = re.findall(r'^\s*set\((\w+)\s+"([^"]*)"\)', text, re.M)
    check([k for k, _ in sets] == ['WOX_VERSION', 'SETUP_VERSION'], 'versions.cmake sets WOX_VERSION and SETUP_VERSION once each')
    check(dict(sets).get('WOX_VERSION') == wv and dict(sets).get('SETUP_VERSION') == sv,
          f'the build was configured from versions.cmake (wox {wv}, setup {sv})')

    # 2. the syntax
    def tuple_of(value):
        """The tuple of VALUE as versions.cmake would write it (set(X "VALUE")), or None when it is refused."""
        with tempfile.TemporaryDirectory(prefix='xeen-p338-syntax-') as scratch:
            return _tuple_in(Path(scratch) / 'check.cmake', value)

    def _tuple_in(script, value):
        script.write_text(f'include("{(ROOT / "cmake/xeen_version.cmake").as_posix()}")\n'
                          f'xeen_version_tuple(TEST_VERSION "{value}" tuple)\nmessage("tuple=${{tuple}}")\n')
        r = subprocess.run([cmake, '-P', str(script)], capture_output=True, text=True, timeout=30)
        m = re.search(r'tuple=(\S+)', r.stdout + r.stderr)
        return (m[1] if m else None) if r.returncode == 0 else None

    good = {'1.0.0': '1,0,0,0', '0.0.0': '0,0,0,0', '1.2.3.4': '1,2,3,4', '10.20.30': '10,20,30,0',
            '65535.65535.65535.65535': '65535,65535,65535,65535', wv: None, sv: None}
    for value, want in good.items():
        got = tuple_of(value)
        check(got is not None and (want is None or got == want), f'version {value!r} accepted ({got})')
    for value in ('1.0', '1', '1.0.0.0.0', '01.0.0', '1.00.0', '1.0.0-rc1', '1.0.0+git', 'v1.0.0', ' 1.0.0', '1.0.0 ',
                  '1..0', '1.0.', '-1.0.0', '65536.0.0', '1.0.0.70000', '99999999999999999999.0.0', '1,0,0', ''):
        check(tuple_of(value) is None, f'version {value!r} rejected')

    # 3. the generated headers
    for name, value, other in (('wox', wv, 'SETUP'), ('setup', sv, 'WOX')):
        h = build / 'version' / name / f'xeen_{name}_version.h'
        body = h.read_text() if h.exists() else ''
        check(f'#define XEEN_{name.upper()}_VERSION "{value}"' in body and f'XEEN_{other}_VERSION' not in body,
              f'{h.relative_to(build)} defines XEEN_{name.upper()}_VERSION "{value}" only')
        rc = build / 'version' / name / f'{name}_version.rc'
        body = rc.read_text() if rc.exists() else ''
        t = tuple_of(value)
        check(f'FILEVERSION {t}' in body and f'PRODUCTVERSION {t}' in body and f'"FileVersion", "{value}"' in body
              and f'"ProductVersion", "{value}"' in body, f'{rc.relative_to(build)}: {value} and {t}')

    # 4. one source
    literal = re.compile(r'session start: WoX \(\d|WoX - Setup \(\d|VERSIONINFO|"FileVersion"'
                         r'|#define\s+XEEN_(WOX|SETUP)_VERSION\b')
    offenders = []
    for base in ('src', 'include', 'tools/windows/package.sh', 'tools/linux/package.sh', 'CMakeLists.txt'):
        p = ROOT / base
        for f in ([p] if p.is_file() else [q for q in p.rglob('*') if q.is_file() and q.suffix in ('.c', '.h', '.rc', '.inc', '.sh', '.txt')]):
            if literal.search(f.read_text(errors='replace')):
                offenders.append(str(f.relative_to(ROOT)))
    check(not offenders, f'no hand-written version text or resource outside versions.cmake and cmake/ ({offenders})')
    win = (ROOT / 'src/setup/setup_win32.c').read_text()
    gtk = (ROOT / 'src/setup/setup_gtk.c').read_text()
    check('top_window(main_class, SETUP_MAIN_TITLE_W,' in function_body(win, 'build_window'),
          'Win32 setup: the main window (build_window) is titled SETUP_MAIN_TITLE_W')
    check('gtk_window_set_title(GTK_WINDOW(a->window), SETUP_MAIN_TITLE);' in function_body(gtk, 'build_window'),
          'GTK setup: the main window (build_window) is titled SETUP_MAIN_TITLE')
    check('XEEN_WOX_VERSION' not in win + gtk + (ROOT / 'src/setup/setup_title.h').read_text(),
          "setup never reads the game's version")

    with tempfile.TemporaryDirectory(prefix='xeen-p338-') as t:
        t = Path(t)
        game, empty, home = t / 'game', t / 'empty', t / 'home'
        for d in (game, empty, home):
            d.mkdir()
        x = game / 'wox'
        shutil.copy(wox, x)
        before = tree(t)
        # 5. --version
        nowhere = t / 'nowhere'
        for args in (['--version'], ['--headless', '--version'], ['--fullscreen', '--bogus', '--version'],
                     ['--wox-config', str(t / 'made.cfg'), '--version'], ['--version', '--data-dir', str(nowhere)],
                     ['--version', '--help'], ['--diagnostic-state', '--version', '--menu']):
            r = run(x, args, home, empty)
            check(r.returncode == 0 and r.stdout == wv + '\n' and r.stderr == '',
                  f'wox {" ".join(args)}: "{wv}\\n" on stdout, status 0, stderr empty ({r.returncode}, {r.stdout!r}, {r.stderr[:80]!r})')
        r = run(x, ['--version'], home, empty, {'XEEN_PROGRAM_DATA_DIR': str(nowhere)})
        check(r.returncode == 0 and r.stdout == wv + '\n', '--version needs nothing from the environment')
        check(tree(t) == before, f'--version created no file ({sorted(set(tree(t)) - set(before))})')
        # 6. --help and the log
        r = run(x, ['--headless', '--help'], home, empty)
        check(r.returncode == 0 and '[--version (print the version number and exit' in r.stdout, '--help names --version')
        r = run(x, ['--data-dir', str(ROOT / 'dos'), '--no-audio'], home, empty)
        lines = (game / 'wox.log').read_text().splitlines() if (game / 'wox.log').exists() else []
        starts = [l for l in lines if 'session start' in l]
        first = LINE.match(lines[0]) if lines else None
        check(r.returncode == 1 and first is not None and first[1] == f'session start: WoX ({wv})'
              and len(starts) == 1, f'wox.log: the session starts with "session start: WoX ({wv})", once')
        check(len(lines) > 2 and LINE.match(lines[1]) and lines[1].endswith(f'INFO  log file: {game / "wox.log"}'),
              'the log path still follows it')
        r = run(x, ['--version'], home, empty)
        check(r.stdout == wv + '\n' and len((game / 'wox.log').read_text().splitlines()) == len(lines),
              '--version after a session: wox.log untouched')

    # 7. the binaries
    data = wox.read_bytes()
    check(f'session start: WoX ({wv})'.encode() in data and f'{wv}\n'.encode() in data,
          'wox holds its version texts')
    check(b'WoX - Setup (' not in data, "wox holds no setup title")
    if setup:
        data = setup.read_bytes()
        check(f'WoX - Setup ({sv})'.encode() in data, f'setup holds "WoX - Setup ({sv})"')
        check(b'session start: WoX' not in data, "setup holds no game log text")
    print('FAIL' if FAILS else 'PASS', f'({len(FAILS)} failure(s))')
    return 1 if FAILS else 0


if __name__ == '__main__':
    sys.exit(main())
