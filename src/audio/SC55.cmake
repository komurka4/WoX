# SPDX-License-Identifier: GPL-3.0-or-later

# Configure the pinned SC-55 backend as a separate shared library. xeen_add_sc55 builds its
# private implementation and the versioned C adapter for the player loader; ROMs remain runtime
# inputs.

# Build only the GPL-2.0-or-later Nuked-SC55 backend. No RtMidi, SDL frontend,
# installation, ROM bundling or configure-time network access is required.
enable_language(CXX)
if(NOT EXISTS "${XEEN_SC55_SOURCE_DIR}/src/backend/emu.cpp")
 message(FATAL_ERROR "SC-55 source missing; set XEEN_SC55_SOURCE_DIR")
endif()
function(xeen_add_sc55)
set(sc55_backend "${XEEN_SC55_SOURCE_DIR}/src/backend")
set(NUKED_ENABLE_DECODER2 ON)
set(NUKED_ENABLE_ASIO OFF)
set(NUKED_SOURCE "XEEN pinned 66d12727cc43000d38094aaf2f76dc30cc93f448")
set(CMAKE_PROJECT_VERSION 0.8.0)
set(CMAKE_PROJECT_VERSION_MAJOR 0)
set(CMAKE_PROJECT_VERSION_MINOR 8)
set(CMAKE_PROJECT_VERSION_PATCH 0)
configure_file("${sc55_backend}/config.h.in" "${CMAKE_CURRENT_BINARY_DIR}/sc55/backend/config.h" @ONLY)
set(sc55_sources config diagnostics emu file_hashing file_io lcd mcu_interrupt mcu_opcodes mcu_timer mcu pcm rom_io rom standard_romsets submcu)
set(sc55_paths)
foreach(name IN LISTS sc55_sources)
 list(APPEND sc55_paths "${sc55_backend}/${name}.cpp")
endforeach()
# This pinned directory contains only decoder implementation translation units.
file(GLOB sc55_decoder_sources "${sc55_backend}/decoder2/*.cpp")
add_library(xeen_sc55 SHARED ${sc55_paths} ${sc55_decoder_sources}
 "${sc55_backend}/sha/sha224-256.c" "${PROJECT_SOURCE_DIR}/src/audio/sc55.cpp")
target_compile_features(xeen_sc55 PRIVATE cxx_std_23)
target_include_directories(xeen_sc55 PUBLIC "${PROJECT_SOURCE_DIR}/src/audio"
 PRIVATE "${sc55_backend}" "${CMAKE_CURRENT_BINARY_DIR}/sc55/backend")
# Only the small versioned C ABI is exported. C++ implementation stays private.
target_compile_definitions(xeen_sc55 PRIVATE XEEN_SC55_BUILD)
set_target_properties(xeen_sc55 PROPERTIES CXX_VISIBILITY_PRESET hidden
 C_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN YES)
if(WIN32)
 set_target_properties(xeen_sc55 PROPERTIES PREFIX "" OUTPUT_NAME "xeen_sc55")
 # Optimize the backend and discard unreachable sections. The GCC runtime is linked in, so the
 # DLL needs only KERNEL32 and msvcrt, and none of its symbols is exported. LTO runs as one
 # partition: GCC 13 MinGW's default partitioned LTO link of this backend fails.
 if(MINGW)
  target_compile_options(xeen_sc55 PRIVATE -Os -ffunction-sections -fdata-sections -flto -flto-partition=one)
  target_link_options(xeen_sc55 PRIVATE -Os -flto -flto-partition=one -static-libgcc -static-libstdc++
   -Wl,--gc-sections,--exclude-libs,ALL,--no-insert-timestamp)
 endif()
elseif(UNIX)
 set_target_properties(xeen_sc55 PROPERTIES VERSION 1.0.0 SOVERSION 1)
endif()
if(XEEN_SANITIZE AND NOT MSVC)
 target_compile_options(xeen_sc55 PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
 target_link_options(xeen_sc55 PUBLIC -fsanitize=address,undefined)
endif()
unset(sc55_backend)
unset(sc55_sources)
unset(sc55_paths)
unset(sc55_decoder_sources)
endfunction()
xeen_add_sc55()
