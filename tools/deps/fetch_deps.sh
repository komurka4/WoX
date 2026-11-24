#!/bin/bash
# Fetch the pinned third-party audio sources into DEPS (default: deps/ in the source tree), verify them, and on a
# Linux host build Munt's libmt32emu for the Linux build. Nothing here is a game file or a ROM.
#
#   SDL2 2.30.11        release tarball, SHA-256 checked          -> DEPS/sdl2-src-2.30.11
#   Munt 2.8.3          tag libmt32emu_2_8_3 tarball, SHA-256     -> DEPS/mt32emu/munt-libmt32emu_2_8_3
#   ymfm                git commit 81aec25c (checked)             -> DEPS/ymfm-src
#   Nuked-SC55          git commit 66d12727 (jcmoyer fork)        -> DEPS/nuked-sc55-src
#   libmt32emu (Linux)  built from the Munt source above          -> DEPS/mt32emu-linux  (skipped with --no-munt-build)
#
# Licences: THIRD_PARTY_NOTICES.md. Requires curl, tar, git, sha256sum, cmake and a C++ compiler (for Munt).
# Usage: tools/deps/fetch_deps.sh [--no-munt-build] [DEPS]
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
BUILD_MUNT=1
if [ "${1:-}" = "--no-munt-build" ]; then BUILD_MUNT=0; shift; fi
DEPS=${1:-$ROOT/deps}
JOBS=${JOBS:-3}
mkdir -p "$DEPS/downloads"
cd "$DEPS"

fetch() { # URL FILE SHA256
  if [ ! -f "downloads/$2" ]; then
    curl -fL --retry 3 -o "downloads/$2.part" "$1"
    mv "downloads/$2.part" "downloads/$2"
  fi
  echo "$3  downloads/$2" | sha256sum -c -
}

git_at() { # URL COMMIT DIR
  if [ ! -d "$3/.git" ]; then
    rm -rf "$3.part"
    git init -q "$3.part"
    git -C "$3.part" fetch -q --depth 1 "$1" "$2"
    git -C "$3.part" checkout -q FETCH_HEAD
    mv "$3.part" "$3"
  fi
  test "$(git -C "$3" rev-parse HEAD)" = "$2" || { echo "$3: not at $2" >&2; exit 1; }
  test -z "$(git -C "$3" status --porcelain)" || { echo "$3: modified working tree" >&2; exit 1; }
  echo "$3: $2 OK"
}

fetch https://github.com/libsdl-org/SDL/releases/download/release-2.30.11/SDL2-2.30.11.tar.gz \
  SDL2-2.30.11.tar.gz 8b8d4aef2038533da814965220f88f77d60dfa0f32685f80ead65e501337da7f
[ -d sdl2-src-2.30.11 ] || { tar -xzf downloads/SDL2-2.30.11.tar.gz && mv SDL2-2.30.11 sdl2-src-2.30.11; }

fetch https://github.com/munt/munt/archive/refs/tags/libmt32emu_2_8_3.tar.gz \
  munt-libmt32emu_2_8_3.tar.gz 81f8c462f46bc8901618762ae34cf9de93894ff81f41db73c79472fa3baef875
[ -d mt32emu/munt-libmt32emu_2_8_3 ] || { mkdir -p mt32emu && tar -xzf downloads/munt-libmt32emu_2_8_3.tar.gz -C mt32emu; }

git_at https://github.com/aaronsgiles/ymfm.git 81aec25ccbb98f4873a255f7551ac4dadac59b4a ymfm-src
git_at https://github.com/jcmoyer/Nuked-SC55.git 66d12727cc43000d38094aaf2f76dc30cc93f448 nuked-sc55-src

# Linux: libmt32emu.so.2 (loaded at run time by wox; packaged beside it) and its C headers, built as the shared
# C-interface library from the pinned source.
if [ "$BUILD_MUNT" = 1 ] && [ "$(uname -s)" = Linux ] && [ ! -f mt32emu-linux/lib/libmt32emu.so.2 ]; then
  cmake -S mt32emu/munt-libmt32emu_2_8_3/mt32emu -B mt32emu/build-linux -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX="$DEPS/mt32emu-linux" -DCMAKE_INSTALL_LIBDIR=lib \
    -Dlibmt32emu_SHARED=ON -Dlibmt32emu_C_INTERFACE=ON -Dlibmt32emu_CPP_INTERFACE=OFF \
    -Dlibmt32emu_REQUIRE_ANSI=ON -Dlibmt32emu_WITH_INTERNAL_RESAMPLER=ON \
    -Dlibmt32emu_WITH_SYMBOL_VERSIONING=OFF -Dlibmt32emu_WITH_VERSION_TAGGING=OFF >/dev/null
  cmake --build mt32emu/build-linux -j"$JOBS"
  cmake --install mt32emu/build-linux >/dev/null
fi
echo "dependencies ready in $DEPS"
