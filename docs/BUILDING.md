# Building `wox` and `setup`

Everything below was run on **Ubuntu 24.04 LTS (WSL2)** with GCC 13.3, CMake 3.28.3, Python 3.12 and, for Windows,
MinGW-w64 GCC 13 (win32 thread model). Linux binaries are built natively; Windows binaries are **cross-compiled**
from Linux. Other distributions, compilers, and native Windows toolchains (MSVC, MSYS2) have not been tested.

## 1. Requirements

| Needed for | Ubuntu/Debian packages |
|---|---|
| everything | `build-essential` (GCC/G++ 13 tested; the SC-55 backend needs C++23), `cmake` (3.16+, 3.28 tested), `python3`, `python3-venv`, `pkg-config`, `git`, `curl` |
| `wox` on Linux | `libx11-dev` |
| `setup` on Linux | `libgtk-3-dev` (without it CMake skips `setup` with a message) |
| audio in a developer build | `libsdl2-dev` (without SDL 2 the game builds with a silent audio backend) |
| audio in the Linux **package** (SDL built from source) | `libasound2-dev`, `libpulse-dev` — SDL only enables the ALSA and PulseAudio backends when their headers exist at build time |
| Windows cross-build | `gcc-mingw-w64-x86-64-win32`, `g++-mingw-w64-x86-64-win32`, `binutils-mingw-w64-x86-64` (the toolchain file is `cmake/mingw-w64.cmake`) |

```sh
sudo apt install build-essential cmake python3 python3-venv pkg-config git curl \
                 libx11-dev libgtk-3-dev libsdl2-dev libasound2-dev libpulse-dev
sudo apt install gcc-mingw-w64-x86-64-win32 g++-mingw-w64-x86-64-win32 binutils-mingw-w64-x86-64
```

## 2. The program data (once, from your copy of the game)

The engine's initial data segment comes from the original `XEEN.DAT` and is not distributed. Generate it as
described in [src/data/README.md](../src/data/README.md). The extractor needs the Capstone Python module; with a
virtual environment (tested with Capstone 5.0.7):

```sh
mkdir -p build && tools/data/unpack_xeen_dat.sh /path/to/XEEN.DAT build/XEEN.unpacked.DAT
python3 -m venv build/venv
build/venv/bin/pip install capstone
build/venv/bin/python -B tools/data/extract_program_data.py --xeen-dat build/XEEN.unpacked.DAT \
    --out src/data --meta build/program-data-meta
```

To keep the data outside the source tree, generate it into another directory and set
`-DXEEN_PROGRAM_DATA_DIR=DIR` (CMake) or the environment variable `XEEN_PROGRAM_DATA_DIR=DIR` (package scripts).

`unpack_xeen_dat.sh` downloads deark 1.7.3 (MIT licence; SHA-256 checked) into `deps/`, builds it (needs `make` and
a C compiler), unpacks the PKLITE-compressed `XEEN.DAT` and refuses any result other than the exact expected file.
An already unpacked `XEEN.DAT` is copied unchanged. deark is not part of WoX or its packages. **GOG.com:** pass
`WORLD\XEEN.DAT` from the installation folder; it is converted without deark (`tools/data/gog_xeen_dat.py`, see
[src/data/README.md](../src/data/README.md#the-packed-xeendat)).

## 3. Third-party sources

```sh
tools/deps/fetch_deps.sh            # into deps/ (or: tools/deps/fetch_deps.sh /other/dir, then XEEN_AUDIO_DEPS=/other/dir)
```

It downloads and verifies SDL 2.30.11 and Munt 2.8.3 (SHA-256 of the release archives), ymfm and Nuked-SC55 (exact
git commits), and on Linux builds Munt's `libmt32emu.so.2` into `deps/mt32emu-linux`. Nothing is patched. Versions,
licences and obligations: [THIRD_PARTY_NOTICES.md](../THIRD_PARTY_NOTICES.md).

## 4. Packages

```sh
tools/linux/package.sh        # build/linux-package/dist/
tools/windows/package.sh      # build/windows-package/dist/
```

Each script first builds an audio-only static SDL 2 from `deps/`, then the programs and the synth libraries in
Release, strips them and copies them with the licence texts into `dist/`:

| Linux `dist/` | Windows `dist/` | Contents |
|---|---|---|
| `wox` | `wox.exe` | the game, SDL linked in; Linux needs only `libX11`, `libm`, `libc` (run path `$ORIGIN`); Windows imports system DLLs only (GUI subsystem) |
| `setup` | `setup.exe` | the settings tool (Linux: GTK 3 from the system; Windows: system DLLs only, statically linked runtime) |
| `libxeen_sbpro.so.1` | `xeen_sbpro.dll` | AdLib / Sound Blaster music (ymfm) |
| `libxeen_sc55.so.1` | `xeen_sc55.dll` | Roland SC-55 music (Nuked-SC55) |
| `libmt32emu.so.2` | `xeen_mt32.dll` | Roland MT-32 music (Munt) |
| `licenses/` | `licenses\` | `LICENSE`, `THIRD_PARTY_NOTICES.md` and the third-party licence texts |

The Linux script checks that every library a file needs is beside it or a standard system library; the Windows
script runs `tools/windows/check_dlls.py` (imports and exports of each file). `tools/windows/check_versions.py
build/windows-package/dist` reads back the version resources. `JOBS=N` sets the parallel build jobs (default 3).

Before distributing a package, see the corresponding-source obligations in
[THIRD_PARTY_NOTICES.md](../THIRD_PARTY_NOTICES.md); the release script below meets them.

### Release archives

```sh
tools/release/make_release.py --build          # or without --build when both dist/ folders are current
tools/release/make_release.py --only linux     # one platform
```

Output in `build/release/` (`--out DIR`), named after `WOX_VERSION` in `versions.cmake`:

```
wox-1.0.0-windows-x64.zip  /  wox-1.0.0-linux-x64.tar.gz   (no enclosing folder: unpack into the folder you want)
  wox, setup and their libraries            (exactly the package's programs)
  extras/README.md, extras/docs/            user documentation
  extras/licenses/                          LICENSE, THIRD_PARTY_NOTICES.md, third-party licence texts
  extras/source/wox-1.0.0-src.tar.gz        the complete source of this version
  extras/source/third-party/*.tar.gz        SDL 2.30.11, Munt 2.8.3, ymfm, Nuked-SC55 as used (pins re-checked)
  extras/source/SOURCE.txt                  what these are and how to rebuild
```

The source archive comes from `git ls-files` in a Git checkout (otherwise from the tree minus `build/`, `deps/` and
other generated folders). The script refuses to run if the generated program data is tracked, or if any file looks
like a game file, ROM, disc image, save or binary (by name).

## 5. Developer builds and tests

```sh
cmake -S . -B build/dev -DCMAKE_BUILD_TYPE=Release      # or Debug
cmake --build build/dev -j
ctest --test-dir build/dev --output-on-failure
```

This uses the system's SDL 2 (dynamically) and builds the synth libraries from `deps/` when present. Useful CMake
options:

| Option | Default | Meaning |
|---|---|---|
| `XEEN_PROGRAM_DATA_DIR` | `src/data` | where the generated program data is |
| `XEEN_AUDIO_DEPS` | `deps` | where `fetch_deps.sh` put the third-party sources |
| `XEEN_AUDIO` | `ON` | build the audio engine (needs SDL 2) |
| `XEEN_SC55` | `ON` | build the SC-55 library (needs the Nuked-SC55 sources) |
| `XEEN_MT32_LIBRARY` | empty | an absolute `libmt32emu` path to try first (developer convenience; packages leave it empty) |
| `BUILD_TESTING` | `ON` | the public test set ([tests/tests.cmake](../tests/tests.cmake)) |

In a developer build, `wox` finds `libxeen_sbpro.so.1` and `libxeen_sc55.so.1` beside itself (they are built into the
same directory). Munt is opened by name through the system's library search, so for MT-32 music either configure
with `-DXEEN_MT32_LIBRARY=$PWD/deps/mt32emu-linux/lib/libmt32emu.so.2` or set the environment variable of the same
name to that file when starting `wox` (the packages instead put `libmt32emu.so.2` beside `wox`, whose run path is
`$ORIGIN`).

Other targets: `xeen_native` is a line-mode developer console for the engine (not needed to play).

## 6. Continuous integration

`.github/workflows/ci.yml` builds every push and pull request on GitHub: Linux (all targets, the tests that do not
need the real program data, the package check) and the Windows cross-build with its DLL and version checks. Since
the program data must never be uploaded, CI configures with `-DXEEN_PROGRAM_DATA_STUB=ON`, an all-zero placeholder
that lets everything compile and link but does not make a working game; tests that read the real data carry the CTest
label `needs-program-data` and are skipped (`ctest -LE needs-program-data`). `tools/release/make_release.py`
refuses binaries built with the placeholder.

## 7. Running

See the README's [first run](../README.md#first-run) and [CONFIGURATION.md](CONFIGURATION.md). The game reads
`XEEN.CC`, `DARK.CC` (required), `INTRO.CC`, `XEEN.CFG` and saves from your game folder; it never reads `XEEN.DAT`.
