#!/bin/bash
# The Windows folder: wox.exe, setup.exe (Phase 331: the settings tool; system DLLs only) and three DLLs, each
# holding only what the game uses.
#   wox.exe         with SDL 2.30.11 linked in (Phase 335, owner request: no SDL2.dll), built here as a static
#                   library: audio (WASAPI, WinMM fallback), threads, timers, atomics, library loading and the
#                   base path; no video, input or dynamic API
#   xeen_sbpro.dll  SB Pro / AdLib music (ymfm)            } GCC runtime linked in: each needs
#   xeen_sc55.dll   Roland SC-55 music (Nuked-SC55)         } only KERNEL32 and msvcrt
#   xeen_mt32.dll   Roland MT-32 music (Munt 2.8.3)         }
#   licenses\       LICENSE, THIRD_PARTY_NOTICES.md and the third-party licence texts
# Cross-builds (MinGW-w64, e.g. from Linux or WSL) in BUILD (default build/windows-package) from the pinned sources in
# XEEN_AUDIO_DEPS (default deps/, tools/deps/fetch_deps.sh) and
# leaves the stripped program and DLLs in BUILD/dist (the tests run on Linux through CTest; none
# is built for Windows). check_dlls.py then checks the folder.
# XEEN_PROGRAM_DATA_DIR (environment): the generated program data when it is not in src/data.
# XEEN_CMAKE_ARGS (environment): extra CMake arguments (CI passes -DXEEN_PROGRAM_DATA_STUB=ON; such a package is
# only a build check: the game does not work and tools/release/make_release.py refuses it).
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
BUILD=${1:-$ROOT/build/windows-package}
DEPS=${XEEN_AUDIO_DEPS:-$ROOT/deps}
JOBS=${JOBS:-3}
TOOLCHAIN=$ROOT/cmake/mingw-w64.cmake
SDL_SRC=$DEPS/sdl2-src-2.30.11
SDL_PREFIX=$BUILD/sdl2-prefix

# SDL2, a static library linked into wox.exe (Phase 335; before it SDL2.dll). The game calls no SDL video, event
# or input function (its window is native Win32), so video and input are off; the event subsystem stays, because
# SDL_InitSubSystem(SDL_INIT_AUDIO) starts it ("audio implies events", SDL.c) and fails without it.
# SDL_THREAD_SAFETY_ANALYSIS is the switch SDL_dynapi.h honours to disable the dynamic API without patching SDL (it
# has no other effect under GCC); the empty DECLSPEC exports nothing from wox.exe. Each function in its own section:
# wox's link keeps only what it reaches. No LTO here: the static archive's objects must stay plain objects for the
# game's (non-LTO) link.
sdl_c="-Os -DNDEBUG -ffunction-sections -fdata-sections -DSDL_THREAD_SAFETY_ANALYSIS -DDECLSPEC="
cmake -S "$SDL_SRC" -B "$BUILD/sdl2" -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN" -DCMAKE_BUILD_TYPE=MinSizeRel \
  -DCMAKE_POLICY_DEFAULT_CMP0069=NEW -DCMAKE_INTERPROCEDURAL_OPTIMIZATION=ON -DCMAKE_C_FLAGS_MINSIZEREL="$sdl_c" \
  -DCMAKE_INSTALL_PREFIX="$SDL_PREFIX" \
  -DSDL_SHARED=OFF -DSDL_STATIC=ON -DSDL_TEST=OFF -DSDL2_DISABLE_SDL2MAIN=ON -DSDL2_DISABLE_UNINSTALL=ON \
  -DSDL_LIBC=ON -DSDL_CCACHE=OFF \
  -DSDL_AUDIO=ON -DSDL_THREADS=ON -DSDL_TIMERS=ON -DSDL_LOADSO=ON -DSDL_FILESYSTEM=ON -DSDL_ATOMIC=ON \
  -DSDL_CPUINFO=ON -DSDL_WASAPI=ON -DSDL_EVENTS=ON \
  -DSDL_VIDEO=OFF -DSDL_RENDER=OFF -DSDL_JOYSTICK=OFF -DSDL_HAPTIC=OFF -DSDL_HIDAPI=OFF \
  -DSDL_POWER=OFF -DSDL_FILE=OFF -DSDL_SENSOR=OFF -DSDL_LOCALE=OFF -DSDL_MISC=OFF \
  -DSDL_DIRECTX=OFF -DSDL_XINPUT=OFF -DSDL_RENDER_D3D=OFF -DSDL_OPENGL=OFF -DSDL_OPENGLES=OFF \
  -DSDL_VULKAN=OFF -DSDL_DISKAUDIO=OFF -DSDL_DUMMYAUDIO=OFF -DSDL_DUMMYVIDEO=OFF -DSDL_OFFSCREEN=OFF >/dev/null
cmake --build "$BUILD/sdl2" -j"$JOBS"
cmake --install "$BUILD/sdl2" >/dev/null

# The program and the three synth DLLs.
# Source paths compiled into the binaries (__FILE__ in assertions and diagnostics, e.g. Nuked-SC55's file_io.cpp)
# are made relative, so a package never carries the build machine's directories.
prefix_map="-ffile-prefix-map=$DEPS/=deps/ -ffile-prefix-map=$ROOT/="
cmake -S "$ROOT" -B "$BUILD" -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN" -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_TESTING=OFF -DXEEN_AUDIO_DEPS="$DEPS" -DXEEN_SC55=ON -DXEEN_PROGRAM_DATA_STUB=OFF -DXEEN_MT32_LIBRARY= ${XEEN_PROGRAM_DATA_DIR:+-DXEEN_PROGRAM_DATA_DIR="$XEEN_PROGRAM_DATA_DIR"} ${XEEN_CMAKE_ARGS:-} \
  -DCMAKE_C_FLAGS="$prefix_map" -DCMAKE_CXX_FLAGS="$prefix_map" \
  -DSDL2_INCLUDE_DIR="$SDL_PREFIX/include/SDL2" -DSDL2_LIBRARY="$SDL_PREFIX/lib/libSDL2.a" >/dev/null
cmake --build "$BUILD" -j"$JOBS" --target wox setup xeen_sbpro xeen_sc55 xeen_mt32

# The folder to copy to Windows: the program and its DLLs only.
rm -rf "$BUILD/dist"
mkdir "$BUILD/dist"
cp "$BUILD/wox.exe" "$BUILD/setup.exe" "$BUILD/xeen_sbpro.dll" "$BUILD/xeen_sc55.dll" \
  "$BUILD/xeen_mt32.dll" "$BUILD/dist/"
x86_64-w64-mingw32-strip --strip-all "$BUILD"/dist/*
# The licence texts and notices travel with the binaries (THIRD_PARTY_NOTICES.md lists each component's terms).
mkdir "$BUILD/dist/licenses"
cp "$ROOT/LICENSE" "$ROOT/THIRD_PARTY_NOTICES.md" "$BUILD/dist/licenses/"
cp "$ROOT"/LICENSES/* "$BUILD/dist/licenses/"
python3 -B "$ROOT/tools/windows/check_dlls.py" "$BUILD/dist"
echo "dist: $BUILD/dist ($(du -sh "$BUILD/dist" | cut -f1))"
