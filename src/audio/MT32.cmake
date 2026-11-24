# SPDX-License-Identifier: GPL-3.0-or-later

# xeen_mt32.dll: the pinned Munt 2.8.3 (libmt32emu, LGPL-2.1-or-later) built from its sources for
# the Windows package, with its GCC runtime inside, so the DLL needs only KERNEL32 and msvcrt.
# Its only export is mt32emu_get_service_i, the entry src/audio/mt32.c loads. The build follows
# upstream's C-interface-only configuration with three differences that drop code the game
# never reaches:
#  - no sample-rate converter: the game renders at Munt's native 32000 Hz (COARSE analogue
#    output, no set_stereo_output_samplerate), where Munt's SampleRateConverter renders
#    straight from the synth (useSynthDelegate) and never calls a resampler;
#  - no version tagging (a shared-object symbol check, unused by mt32.c);
#  - FileStream from src/audio/mt32_file_stream.cpp (C stdio) instead of upstream's
#    std::ifstream one, so the static C++ runtime carries no iostreams or locales.
# GCC 13 MinGW fails Munt's LTO build (an internal compiler error in BReverbModel), so this
# build uses -Os and section collection without LTO.
if(NOT EXISTS "${XEEN_MT32_SOURCE_DIR}/src/c_interface/c_interface.cpp")
 message(FATAL_ERROR "Munt source missing; set XEEN_MT32_SOURCE_DIR")
endif()
function(xeen_add_mt32)
set(mt32_src "${XEEN_MT32_SOURCE_DIR}/src")
set(mt32_gen "${CMAKE_CURRENT_BINARY_DIR}/mt32")
include("${XEEN_MT32_SOURCE_DIR}/cmake/project_data.cmake")   # libmt32emu_VERSION*
set(libmt32emu_EXPORTS_TYPE 1)            # C interface only
set(libmt32emu_SHARED_DEFINITION "")      # no dllexport: xeen_mt32.def names the export
set(libmt32emu_RUNTIME_VERSION_CHECK 0)
configure_file("${mt32_src}/config.h.in" "${mt32_gen}/config.h" @ONLY)
# The C headers the executable's mt32.c includes as <mt32emu/c_interface/c_types.h>, from the
# same source and configuration as the DLL.
set(mt32_api "${mt32_gen}/include/mt32emu")
foreach(name globals.h Enumerations.h Types.h c_interface/c_types.h)
 configure_file("${mt32_src}/${name}" "${mt32_api}/${name}" COPYONLY)
endforeach()
configure_file("${mt32_gen}/config.h" "${mt32_api}/config.h" COPYONLY)
set(XEEN_MT32_GENERATED_INCLUDE "${mt32_gen}/include" PARENT_SCOPE)
set(mt32_sources Analog BReverbModel Display File LA32FloatWaveGenerator LA32Ramp LA32WaveGenerator
 MidiStreamParser Part Partial PartialManager Poly ROMInfo Synth Tables TVA TVF TVP sha1/sha1
 SampleRateConverter c_interface/c_interface)
set(mt32_paths)
foreach(name IN LISTS mt32_sources)
 list(APPEND mt32_paths "${mt32_src}/${name}.cpp")
endforeach()
add_library(xeen_mt32 SHARED ${mt32_paths} "${PROJECT_SOURCE_DIR}/src/audio/mt32_file_stream.cpp"
 "${PROJECT_SOURCE_DIR}/src/audio/xeen_mt32.def")
set_target_properties(xeen_mt32 PROPERTIES PREFIX "" OUTPUT_NAME "xeen_mt32")
target_include_directories(xeen_mt32 PRIVATE "${mt32_gen}" "${mt32_src}")
# Upstream's flags (-ansi -pedantic, std::snprintf found), and the FileStream declaration first.
target_compile_definitions(xeen_mt32 PRIVATE MT32EMU_WITH_STD_SNPRINTF)
target_compile_options(xeen_mt32 PRIVATE -ansi -pedantic -Os -ffunction-sections -fdata-sections
 -include "${PROJECT_SOURCE_DIR}/src/audio/mt32_file_stream.h")
target_link_options(xeen_mt32 PRIVATE -static-libgcc -static-libstdc++
 -Wl,--gc-sections,--exclude-libs,ALL,--no-insert-timestamp)
endfunction()
xeen_add_mt32()
