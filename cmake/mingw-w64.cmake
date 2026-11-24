# Phase78: the MinGW-w64 toolchain for cross-building the Windows executable (e.g. from WSL). The working recipe is
# tools/windows/package.sh: it builds the audio-only SDL2 and passes this file with the SC-55 and MT-32 settings,
# and leaves the program and its four DLLs in BUILD/dist:
#   tools/windows/package.sh [BUILD]        (default build/windows-package)
# (Phase 276: the earlier two-line recipe here, Release with BUILD_TESTING=OFF only, does not link xeen_wox_config.)
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_C_COMPILER x86_64-w64-mingw32-gcc)
set(CMAKE_CXX_COMPILER x86_64-w64-mingw32-g++)      # the SC-55, MT-32 and SB Pro DLLs
set(CMAKE_RC_COMPILER x86_64-w64-mingw32-windres)   # Phase161: the icon resource
set(CMAKE_FIND_ROOT_PATH /usr/x86_64-w64-mingw32)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
