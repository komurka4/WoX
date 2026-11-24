# Third-party components and notices

> This file records what the source tree and the planned binary packages contain from
> other projects, under which terms, and what those terms require. It is a factual inventory prepared from the
> pinned upstream sources, not legal advice. The terms of the original game (World of Xeen) are a separate matter:
> see [README.md](README.md#legal-status-and-disclaimer).

The project's own code is offered under the GNU General Public License, version 3 or any later version
(`GPL-3.0-or-later`, [LICENSE](LICENSE)). The licence texts of the components below are
in [LICENSES/](LICENSES/); the packaging scripts copy `LICENSE`, this file and `LICENSES/` into each package's
`licenses/` folder.

## Components

| Component | Upstream | Pinned revision | Licence (SPDX) | How it is used | In this source tree | In the packages |
|---|---|---|---|---|---|---|
| **SDL 2** | https://github.com/libsdl-org/SDL | release 2.30.11, `SDL2-2.30.11.tar.gz`, SHA-256 `8b8d4aef2038533da814965220f88f77d60dfa0f32685f80ead65e501337da7f` | `Zlib` | audio device, threads, timers (wox's audio engine); video/input are disabled in the package build | not included; fetched by `tools/deps/fetch_deps.sh` | **statically linked into `wox` / `wox.exe`** (package scripts build an audio-only static library). A non-package Linux build links the system's SDL 2 dynamically |
| **Munt (libmt32emu)** | https://github.com/munt/munt | tag `libmt32emu_2_8_3`, archive SHA-256 `81f8c462f46bc8901618762ae34cf9de93894ff81f41db73c79472fa3baef875` | `LGPL-2.1-or-later` | Roland MT-32 emulation; loaded at run time by `wox` (`src/audio/mt32.c`, `dlopen`/`LoadLibrary`) through Munt's C interface | not included (fetched); `src/audio/mt32_file_stream.{cpp,h}` are an adapted copy of Munt's `FileStream` (C stdio instead of `std::ifstream`), under Munt's licence with its notices | Linux: `libmt32emu.so.2`, Munt's own shared C-interface build, unmodified. Windows: `xeen_mt32.dll`, built from the Munt sources by `src/audio/MT32.cmake` with the adapted `FileStream`, without the sample-rate converter and version tagging, GCC runtime linked in |
| **Nuked-SC55** (J.C. Moyer's fork of nukeykt's Nuked-SC55) | https://github.com/jcmoyer/Nuked-SC55 | commit `66d12727cc43000d38094aaf2f76dc30cc93f448` | `GPL-2.0-or-later` (files by nukeykt and J.C. Moyer) | Roland SC-55 emulation backend | not included (fetched) | `libxeen_sc55.so.1` / `xeen_sc55.dll`: upstream `src/backend` sources unmodified plus the project's adapter `src/audio/sc55.cpp` (`src/audio/SC55.cmake`) |
| RFC 6234 SHA-224/256 (inside Nuked-SC55) | https://www.rfc-editor.org/rfc/rfc6234 | as in the Nuked-SC55 commit above (`src/backend/sha/`) | `BSD-3-Clause` (IETF Trust) | file hashing inside the SC-55 backend | not included (fetched) | inside the SC-55 library; notice in `LICENSES/BSD-3-Clause-RFC6234-SHA.txt` |
| **ymfm** | https://github.com/aaronsgiles/ymfm | commit `81aec25ccbb98f4873a255f7551ac4dadac59b4a` | `BSD-3-Clause` (Aaron Giles) | Yamaha OPL2 emulation for the Sound Blaster Pro / AdLib music | not included (fetched) | `libxeen_sbpro.so.1` / `xeen_sbpro.dll`: `ymfm_opl.cpp`, `ymfm_adpcm.cpp`, `ymfm_pcm.cpp` unmodified plus the project's `src/audio/sbpro*.c*`, `fm1_sequence.c` |
| **stb_vorbis** | https://github.com/nothings/stb | v1.22, commit `1ee679ca2ef753a528db5ba6801e1067b40481b8` | `MIT OR Unlicense` (public-domain alternative) | decoding the optional Ogg Vorbis CD-audio rips | **included unmodified**: `src/audio/stb_vorbis.c` (verified byte-identical to upstream) | compiled into `wox` |
| GCC runtime (libgcc, libstdc++) | https://gcc.gnu.org | the toolchain used (GCC 13 tested) | `GPL-3.0-or-later WITH GCC-exception-3.1` | C++ runtime of the synth libraries | — | Windows: linked statically into the three DLLs; Linux: system `libstdc++`/`libgcc_s` |
| MinGW-w64 runtime | https://www.mingw-w64.org | the toolchain used | mixed permissive / public-domain terms (see the toolchain's `COPYING` files) | start-up code and helpers linked into every `.exe`/`.dll` | — | Windows packages |
| System libraries | glibc, libX11 (Linux); GTK 3 and its dependencies (Linux `setup`); ALSA/PulseAudio/PipeWire client libraries loaded by SDL at run time when present; Windows system DLLs | — | each its own (LGPL-2.1+, MIT/X11, ...) | dynamically linked or loaded from the user's system | — | **not** shipped |

Build-time-only tools (not part of any package or release archive): CMake, Python 3, the
[Capstone](https://www.capstone-engine.org/) Python module (`BSD-3-Clause`) used by `tools/data/extract_program_data.py`,
and [deark](https://github.com/jsummers/deark) 1.7.3 (Jason Summers, MIT-style licence; release archive SHA-256
`fddc9e548925ecda60f1e56a23a290d33bc64cfca26f78794c00ecdac9c8d30d`), which `tools/data/unpack_xeen_dat.sh`
downloads and builds to unpack the game's PKLITE-compressed `XEEN.DAT` when the user generates the program data.

## Not included and never distributed

- Original game files (`XEEN.DAT`, `XEEN.EXE`, `*.CC`, `*.CUR`, saves, `XEEN.CFG`), disc images and CD audio.
- The program data generated from `XEEN.DAT` (`src/data/`, see [src/data/README.md](src/data/README.md)).
- Roland MT-32 and SC-55 ROM images. The emulators above need them at run time; the user supplies them. The setup
  tool (`src/setup/setup_roms.c`) offers to **download** ROM archives from third-party archive.org pages at the
  user's request; those ROMs are not part of WoX and are not covered by any licence in this file.

## Obligations when distributing binaries (assessment for the planned packages)

- **The whole of `wox`, `setup` and the synth libraries** is distributed under GPL-3.0 terms (the project's code is
  GPL-3.0-or-later; Nuked-SC55 is GPL-2.0-*or-later*, which permits use under GPL-3.0; Zlib, BSD-3-Clause, MIT and LGPL-2.1+
  components are compatible). Each binary package must therefore be accompanied by, or offer, the **complete
  corresponding source** (GPL-3.0 section 6): this source tree at the exact release revision, **plus** the exact
  third-party sources fetched by `tools/deps/fetch_deps.sh` (SDL 2.30.11, Munt 2.8.3, ymfm `81aec25c`, Nuked-SC55
  `66d12727`), and the scripts used to build them (`tools/deps/`, `tools/linux/package.sh`,
  `tools/windows/package.sh`, `cmake/`, `src/audio/*.cmake`). Pointing only at upstream repositories is not by itself
  a guarantee that those revisions stay available; `tools/release/make_release.py` therefore puts this
  source tree and the four third-party source archives into every release archive (`extras/source/`).
- **Dynamic linking is not treated as an exemption.** Linux `libmt32emu.so.2` and Windows `xeen_mt32.dll` are
  LGPL-2.1+ libraries loaded at run time; the user can replace them (same name and C interface). The modified Munt
  build for Windows is itself covered by the corresponding-source obligation above.
- **Notices:** the BSD-3-Clause (ymfm, RFC 6234) and Zlib (SDL) terms require their copyright notices and licence
  texts in the documentation of binary distributions: `licenses/` in each package carries them.
- **SDL 2 statically linked:** Zlib terms; the notice is shipped although Zlib only requires that the origin is not
  misrepresented and altered versions are marked. SDL is built from the unmodified release with build options only.
- **GCC runtime:** covered by the GCC Runtime Library Exception (no source obligation for the runtime itself).

## Notices

Upstream copyright notices, reproduced from the pinned sources:

- SDL: Copyright (C) 1997-2025 Sam Lantinga <slouken@libsdl.org> — `LICENSES/Zlib-SDL2.txt`.
- Munt: Copyright (C) 2003, 2004, 2005, 2006, 2008, 2009 Dean Beeler, Jerome Fisher; Copyright (C) 2011-2022 Dean
  Beeler, Jerome Fisher, Sergey V. Mikayev — `LICENSES/LGPL-2.1-Munt.txt`.
- Nuked-SC55: Copyright (C) 2021, 2024 nukeykt; Copyright (C) 2024-2026 J.C. Moyer — `LICENSES/GPL-2.0-Nuked-SC55.txt`.
- RFC 6234 code: Copyright (c) 2011 IETF Trust and the persons identified as authors of the code —
  `LICENSES/BSD-3-Clause-RFC6234-SHA.txt`.
- ymfm: Copyright (c) 2021, Aaron Giles — `LICENSES/BSD-3-Clause-ymfm.txt`.
- stb_vorbis: Copyright (c) 2017 Sean Barrett (MIT alternative; public-domain alternative) —
  `LICENSES/MIT-or-PublicDomain-stb_vorbis.txt`.

## Project artwork

`icon/wox_icon.bmp` and `icon/setup_icon.bmp` were generated for this project with ChatGPT (OpenAI) at the owner's
direction and are offered under the project's licence (GPL-3.0-or-later). They are not taken from the original game.

## Reference material that is *not* a dependency

The developers consulted the ScummVM project's Xeen engine as a source of hypotheses about the original behaviour
(cited in a few source comments, e.g. `include/xeen/text_window.h`); no ScummVM code is included or linked as far
as the review for this release found (comment search only; no systematic code-similarity audit was performed).
