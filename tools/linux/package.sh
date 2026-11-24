#!/bin/bash
# Phase 335 (owner request): the Linux folder, the counterpart of tools/windows/package.sh. Leaves in BUILD/dist:
#   wox                 the game, with SDL 2.30.11 linked in (built here as a static library: audio only; its
#                       ALSA, PulseAudio, PipeWire, JACK and sndio backends are loaded at run time when present),
#                       run-path $ORIGIN, so the libraries below are found beside it
#   setup               the settings tool (GTK 3, which the system provides)
#   libxeen_sbpro.so.1  SB Pro / AdLib music (ymfm)
#   libxeen_sc55.so.1   Roland SC-55 music (Nuked-SC55)
#   libmt32emu.so.2     Roland MT-32 music (Munt 2.8.3, built by tools/deps/fetch_deps.sh)
#   licenses/           LICENSE, THIRD_PARTY_NOTICES.md and the third-party licence texts
# Everything else is a standard system library (libc, libm, libstdc++, libgcc_s, libX11; GTK 3 for setup).
# Usage: tools/linux/package.sh [BUILD] [DEST]   (default build/linux-package; DEST: also copy the folder's files there)
# First: tools/deps/fetch_deps.sh (the pinned sources in deps/) and the program data (src/data/README.md).
# XEEN_PROGRAM_DATA_DIR (environment): the generated program data when it is not in src/data.
# XEEN_CMAKE_ARGS (environment): extra CMake arguments (CI passes -DXEEN_PROGRAM_DATA_STUB=ON; such a package is
# only a build check: the game does not work and tools/release/make_release.py refuses it).
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
BUILD=${1:-$ROOT/build/linux-package}
DEST=${2:-}
DEPS=${XEEN_AUDIO_DEPS:-$ROOT/deps}
JOBS=${JOBS:-3}
SDL_SRC=$DEPS/sdl2-src-2.30.11
SDL_PREFIX=$BUILD/sdl2-prefix
MUNT=$DEPS/mt32emu-linux/lib/libmt32emu.so.2

# SDL2, static and position-independent (wox is a PIE). As on Windows: no video, render or input; the event
# subsystem stays, because SDL_InitSubSystem(SDL_INIT_AUDIO) starts it; SDL_THREAD_SAFETY_ANALYSIS turns the dynamic
# API off without patching SDL. The audio backends stay dlopen()ed (SDL_*_SHARED, SDL's default), so wox does not
# depend on them.
sdl_c="-O2 -DNDEBUG -ffunction-sections -fdata-sections -DSDL_THREAD_SAFETY_ANALYSIS"
cmake -S "$SDL_SRC" -B "$BUILD/sdl2" -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_FLAGS_RELEASE="$sdl_c" \
  -DCMAKE_POSITION_INDEPENDENT_CODE=ON -DCMAKE_INSTALL_PREFIX="$SDL_PREFIX" \
  -DSDL_SHARED=OFF -DSDL_STATIC=ON -DSDL_STATIC_PIC=ON -DSDL_TEST=OFF -DSDL2_DISABLE_SDL2MAIN=ON \
  -DSDL2_DISABLE_UNINSTALL=ON -DSDL_LIBC=ON -DSDL_CCACHE=OFF \
  -DSDL_AUDIO=ON -DSDL_THREADS=ON -DSDL_TIMERS=ON -DSDL_LOADSO=ON -DSDL_FILESYSTEM=ON -DSDL_ATOMIC=ON \
  -DSDL_CPUINFO=ON -DSDL_EVENTS=ON \
  -DSDL_VIDEO=OFF -DSDL_RENDER=OFF -DSDL_JOYSTICK=OFF -DSDL_HAPTIC=OFF -DSDL_HIDAPI=OFF -DSDL_POWER=OFF \
  -DSDL_FILE=OFF -DSDL_SENSOR=OFF -DSDL_LOCALE=OFF -DSDL_MISC=OFF -DSDL_X11=OFF -DSDL_WAYLAND=OFF \
  -DSDL_KMSDRM=OFF -DSDL_OPENGL=OFF -DSDL_OPENGLES=OFF -DSDL_VULKAN=OFF -DSDL_DBUS=OFF -DSDL_IBUS=OFF \
  -DSDL_LIBUDEV=OFF -DSDL_DISKAUDIO=OFF -DSDL_DUMMYAUDIO=OFF -DSDL_DUMMYVIDEO=OFF -DSDL_OFFSCREEN=OFF \
  -DSDL_ALSA=ON -DSDL_ALSA_SHARED=ON -DSDL_PULSEAUDIO=ON -DSDL_PULSEAUDIO_SHARED=ON >/dev/null
cmake --build "$BUILD/sdl2" -j"$JOBS"
cmake --install "$BUILD/sdl2" >/dev/null

# The programs and the two synth libraries; run-path $ORIGIN (no build-tree paths). XEEN_MT32_LIBRARY: no absolute
# fallback path compiled in (Munt is found beside wox).
# Source paths compiled into the binaries (__FILE__ in assertions and diagnostics, e.g. Nuked-SC55's file_io.cpp)
# are made relative, so a package never carries the build machine's directories.
prefix_map="-ffile-prefix-map=$DEPS/=deps/ -ffile-prefix-map=$ROOT/="
cmake -S "$ROOT" -B "$BUILD" -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF -DXEEN_AUDIO_DEPS="$DEPS" \
  -DXEEN_SC55=ON -DXEEN_PROGRAM_DATA_STUB=OFF -DXEEN_MT32_LIBRARY= ${XEEN_PROGRAM_DATA_DIR:+-DXEEN_PROGRAM_DATA_DIR="$XEEN_PROGRAM_DATA_DIR"} ${XEEN_CMAKE_ARGS:-} \
  -DCMAKE_BUILD_WITH_INSTALL_RPATH=ON -DCMAKE_INSTALL_RPATH='$ORIGIN' \
  -DCMAKE_C_FLAGS="$prefix_map" -DCMAKE_CXX_FLAGS="$prefix_map" \
  -DSDL2_INCLUDE_DIR="$SDL_PREFIX/include/SDL2" -DSDL2_LIBRARY="$SDL_PREFIX/lib/libSDL2.a" >/dev/null
cmake --build "$BUILD" -j"$JOBS" --target wox setup xeen_sbpro xeen_sc55

rm -rf "$BUILD/dist"
mkdir "$BUILD/dist"
cp "$BUILD/wox" "$BUILD/setup" "$BUILD/dist/"
cp -L "$BUILD/libxeen_sbpro.so.1" "$BUILD/libxeen_sc55.so.1" "$MUNT" "$BUILD/dist/"
strip --strip-unneeded "$BUILD"/dist/*
# The licence texts and notices travel with the binaries (THIRD_PARTY_NOTICES.md lists each component's terms).
mkdir "$BUILD/dist/licenses"
cp "$ROOT/LICENSE" "$ROOT/THIRD_PARTY_NOTICES.md" "$BUILD/dist/licenses/"
cp "$ROOT"/LICENSES/* "$BUILD/dist/licenses/"

# Check: every library a file needs is beside it or a system library from the list.
system='^(libc|libm|libdl|libpthread|librt|libstdc\+\+|libgcc_s|libX11|libxcb|libXau|libXdmcp|ld-linux-x86-64|linux-vdso|libbsd|libmd)\.so'
status=0
for f in wox libxeen_sbpro.so.1 libxeen_sc55.so.1 libmt32emu.so.2; do
  for lib in $(objdump -p "$BUILD/dist/$f" | awk '/NEEDED/{print $2}'); do
    if [ ! -e "$BUILD/dist/$lib" ] && ! [[ $lib =~ $system ]]; then
      echo "FAIL: $f needs $lib" >&2
      status=1
    fi
  done
done
if objdump -p "$BUILD/dist/wox" | grep -q 'NEEDED.*SDL'; then
  echo "FAIL: wox still needs an SDL library" >&2
  status=1
fi
(cd "$BUILD/dist" && ls -l && echo && for f in wox setup *.so.*; do echo "$f: $(objdump -p "$f" | awk '/NEEDED/{printf "%s ", $2}')"; done)
[ "$status" = 0 ] || exit 1
if [ -n "$DEST" ]; then
  # beside, then renamed over: a wox still running there keeps its old file ("Text file busy" otherwise)
  for f in "$BUILD"/dist/wox "$BUILD"/dist/setup "$BUILD"/dist/*.so.*; do
    cp "$f" "$DEST/$(basename "$f").new"
    mv -f "$DEST/$(basename "$f").new" "$DEST/$(basename "$f")"
  done
  echo "copied to $DEST"
fi
