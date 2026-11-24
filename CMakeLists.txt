cmake_minimum_required(VERSION 3.16)
# WoX (wox), a native reconstruction of World of Xeen, and its settings tool (setup). Build instructions: docs/BUILDING.md.
# Before the first build, generate the program data (src/data, from your own copy of the game): src/data/README.md.
# Audio dependencies (SDL2, ymfm, Nuked-SC55, Munt) are fetched at pinned revisions by tools/deps/fetch_deps.sh into
# deps/ (XEEN_AUDIO_DEPS). The comments "Phase NNN" below refer to the project's development history.
project(xeen_reconstruction VERSION 0.1.0 LANGUAGES C)
include(GNUInstallDirs)

# Phase 338: the version numbers of wox and setup, each written once in versions.cmake (an edit there re-configures an
# existing build: CMake tracks included files). Each product gets its own generated header (XEEN_WOX_VERSION in
# xeen_wox_version.h, XEEN_SETUP_VERSION in xeen_setup_version.h) and Windows version resource, in its own directory
# of the build tree, through an interface target that only that product's sources link (xeen_wox_version,
# xeen_setup_version): no engine library sees either number.
include(cmake/xeen_version.cmake)
include(versions.cmake)
function(xeen_version_target target key name description)
    if(NOT DEFINED ${key})
        message(FATAL_ERROR "versions.cmake does not set ${key}")
    endif()
    xeen_version_tuple(${key} "${${key}}" XEEN_VERSION_TUPLE)
    set(XEEN_VERSION_KEY ${key})
    set(XEEN_VERSION_VALUE "${${key}}")
    string(TOUPPER "XEEN_${name}_VERSION" XEEN_VERSION_MACRO)
    set(XEEN_VERSION_GUARD "${XEEN_VERSION_MACRO}_H")
    set(XEEN_VERSION_NAME ${name})
    set(XEEN_VERSION_DESCRIPTION "${description}")
    set(dir "${CMAKE_CURRENT_BINARY_DIR}/version/${name}")
    configure_file(cmake/xeen_version.h.in "${dir}/xeen_${name}_version.h" @ONLY)
    configure_file(cmake/xeen_version.rc.in "${dir}/${name}_version.rc" @ONLY)
    add_library(${target} INTERFACE)
    target_include_directories(${target} INTERFACE "${dir}")
    set(${target}_RC "${dir}/${name}_version.rc" PARENT_SCOPE)
    message(STATUS "${name} version ${${key}} (Windows resource ${XEEN_VERSION_TUPLE})")
endfunction()
xeen_version_target(xeen_wox_version WOX_VERSION wox "WoX")
xeen_version_target(xeen_setup_version SETUP_VERSION setup "WoX - Setup")

# The base library: the character record queries (src/characters/character_record.c). Phase 284 renamed it from
# the historical xeen_phase1; its CTests keep their names phase1_* (the gate configurations and the CTest inventory
# record them).
add_library(xeen_character_record STATIC src/characters/character_record.c)
add_library(xeen::character_record ALIAS xeen_character_record)
set_target_properties(xeen_character_record PROPERTIES
    C_STANDARD 11 C_STANDARD_REQUIRED YES C_EXTENSIONS NO
    EXPORT_NAME character_record)
target_compile_features(xeen_character_record PUBLIC c_std_11)
target_include_directories(xeen_character_record PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)

include(CTest)

# Warnings are private: consumers do not inherit compiler-specific options.
foreach(target IN ITEMS xeen_character_record xeen_character_record_tests)
    if(TARGET ${target})
        if(MSVC)
            target_compile_options(${target} PRIVATE /W4)
        elseif(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
            target_compile_options(${target} PRIVATE
                -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion)
        endif()
    endif()
endforeach()

install(TARGETS xeen_character_record EXPORT xeen_targets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(FILES include/xeen/character_record.h DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/xeen)
install(EXPORT xeen_targets NAMESPACE xeen::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/xeen)

add_library(xeen_movement STATIC src/world/movement.c)
add_library(xeen::movement ALIAS xeen_movement)
set_target_properties(xeen_movement PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED YES C_EXTENSIONS NO EXPORT_NAME movement)
target_include_directories(xeen_movement PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
foreach(target IN ITEMS xeen_movement xeen_movement_tests xeen_scene_prefix_tests)
    if(TARGET ${target})
        if(MSVC)
            target_compile_options(${target} PRIVATE /W4)
        elseif(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
            target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion)
        endif()
    endif()
endforeach()
install(TARGETS xeen_movement EXPORT xeen_targets ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(FILES include/xeen/movement.h DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/xeen)

add_library(xeen_resources STATIC src/core/resources.c)
add_library(xeen::resources ALIAS xeen_resources)
set_target_properties(xeen_resources PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED YES C_EXTENSIONS NO EXPORT_NAME resources)
target_link_libraries(xeen_resources PUBLIC xeen::movement)
target_include_directories(xeen_resources PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(xeen_resources PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion)
endif()
install(TARGETS xeen_resources EXPORT xeen_targets ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(FILES include/xeen/resources.h DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/xeen)

add_library(xeen_events STATIC src/events/event_records.c)
add_library(xeen::events ALIAS xeen_events)
set_target_properties(xeen_events PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED YES C_EXTENSIONS NO EXPORT_NAME events)
target_include_directories(xeen_events PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(xeen_events PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion)
endif()
install(TARGETS xeen_events EXPORT xeen_targets ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(FILES include/xeen/events.h DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/xeen)

# Phase 6: original event selection / bounded operation execution slice.
target_sources(xeen_events PRIVATE src/events/event_exec.c)
# Phase199 add-on damage_numbers: the hooks' queue (no dependencies; event_exec.c,
# combat.c, town.c and game.c report to it). Its overlay is in xeen_game.
target_sources(xeen_events PRIVATE src/addon/damage_numbers/notes.c)
install(FILES include/xeen/event_exec.h DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/xeen)

# Phase 7: native resource/text services; the core interpreter retains no archive dependency.
add_library(xeen_event_resources STATIC src/events/event_text.c src/events/event_resources.c)
add_library(xeen::event_resources ALIAS xeen_event_resources)
set_target_properties(xeen_event_resources PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED YES C_EXTENSIONS NO EXPORT_NAME event_resources)
target_link_libraries(xeen_event_resources PUBLIC xeen::events xeen::resources)
target_include_directories(xeen_event_resources PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
install(TARGETS xeen_event_resources EXPORT xeen_targets ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(FILES include/xeen/event_text.h include/xeen/event_resources.h DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/xeen)
foreach(target IN ITEMS xeen_event_resources xeen_event_expanded_runner xeen_event_text_tests xeen_event_expanded_tests)
    if(TARGET ${target})
        if(MSVC)
            target_compile_options(${target} PRIVATE /W4)
        elseif(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
            target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion)
        endif()
    endif()
endforeach()

# Phase 8 UI provider is optional, with no archive or emulator dependency.
add_library(xeen_event_ui STATIC src/events/event_ui.c)
add_library(xeen::event_ui ALIAS xeen_event_ui)
set_target_properties(xeen_event_ui PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED YES C_EXTENSIONS NO EXPORT_NAME event_ui)
target_link_libraries(xeen_event_ui PUBLIC xeen::events)
install(TARGETS xeen_event_ui EXPORT xeen_targets ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(FILES include/xeen/event_ui.h DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/xeen)
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(xeen_event_ui PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion)
endif()

# Phase 9: terminal frontend and reusable bounded exploration state.
# Phase184: the original's initialized DS image and RNG seed words, generated at
# build time from the human-readable sources in src/data/ (never from XEEN.DAT).
# XEEN_PROGRAM_DATA_DIR selects another copy of those sources (negative controls).
find_package(Python3 COMPONENTS Interpreter REQUIRED)
set(XEEN_PROGRAM_DATA_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src/data" CACHE PATH "Sources of the program data")
set(XEEN_PROGRAM_DATA_JSON manifest.json pointers.json tables.json structs.json opaque.json)
list(TRANSFORM XEEN_PROGRAM_DATA_JSON PREPEND "${XEEN_PROGRAM_DATA_DIR}/")
# Continuous integration only (.github/workflows/ci.yml): an all-zero placeholder instead of the program data, so that
# every target compiles and links without the original game's files. The game does NOT work with it, and
# tools/release/make_release.py refuses binaries built this way.
option(XEEN_PROGRAM_DATA_STUB "CI only: link with an all-zero placeholder instead of the program data (never ship)" OFF)
if(XEEN_PROGRAM_DATA_STUB)
    message(WARNING "XEEN_PROGRAM_DATA_STUB=ON: placeholder program data; the game will not work (CI builds only)")
    set(XEEN_PROGRAM_DATA_C "${CMAKE_CURRENT_BINARY_DIR}/generated/program_data_stub.c")
    file(WRITE "${XEEN_PROGRAM_DATA_C}"
        "/* CI placeholder (XEEN_PROGRAM_DATA_STUB): NOT the program data. The game does not work with it. */\n"
        "#include \"xeen/program_data.h\"\n"
        "const uint8_t xeen_program_ds_image[XEEN_PROGRAM_DS_SIZE];\n"
        "const uint16_t xeen_program_rng_seed[3];\n"
        "const char xeen_program_ds_sha256[65] = \"placeholder: not the program data\";\n")
else()
    # Public source tree: the program data is the original game's initialized data segment, so it is not distributed;
    # each user generates it from their own copy of the game (src/data/README.md).
    foreach(xeen_data_file IN LISTS XEEN_PROGRAM_DATA_JSON)
        if(NOT EXISTS "${xeen_data_file}")
            message(FATAL_ERROR "Program data missing: ${xeen_data_file}\n"
                "Generate src/data from your own copy of the game first (see src/data/README.md), or point "
                "XEEN_PROGRAM_DATA_DIR at a directory that holds it.")
        endif()
    endforeach()
    # Phase 300: the texts (TSV) and the record CSVs; tools/data/meta is metadata, never read by the build.
    file(GLOB XEEN_PROGRAM_DATA_PARTS CONFIGURE_DEPENDS "${XEEN_PROGRAM_DATA_DIR}/texts/*.tsv"
         "${XEEN_PROGRAM_DATA_DIR}/records/*.csv")
    list(APPEND XEEN_PROGRAM_DATA_JSON ${XEEN_PROGRAM_DATA_PARTS})
    set(XEEN_PROGRAM_DATA_C "${CMAKE_CURRENT_BINARY_DIR}/generated/program_data.c")
    add_custom_command(OUTPUT "${XEEN_PROGRAM_DATA_C}"
        COMMAND ${Python3_EXECUTABLE} -B -I "${CMAKE_CURRENT_SOURCE_DIR}/tools/data/gen_program_data.py"
            --data "${XEEN_PROGRAM_DATA_DIR}" --c "${XEEN_PROGRAM_DATA_C}"
        DEPENDS ${XEEN_PROGRAM_DATA_JSON} "${CMAKE_CURRENT_SOURCE_DIR}/tools/data/gen_program_data.py"
        COMMENT "Generating the program data from src/data" VERBATIM)
endif()
add_library(xeen_game STATIC "${XEEN_PROGRAM_DATA_C}" src/game/game.c src/game/game_views.c src/game/game_voice.c src/game/game_event_ui.c src/game/game_events.c src/game/game_event_dialogue.c src/game/game_event_windows.c src/game/game_exit.c src/game/game_scene.c src/game/game_transition.c src/game/game_lifecycle.c src/game/game_resources.c src/game/game_treasure.c src/game/game_fall.c src/game/game_rest.c src/game/game_input.c src/game/game_modal_poll.c src/game/game_commands.c src/game/game_combat_flow.c src/game/game_shoot.c src/game/game_confirm.c src/game/game_scroll.c src/game/game_navigation.c src/game/game_arena.c src/game/game_bank.c src/game/game_guild.c src/game/game_cast.c src/game/game_cast_attack.c src/game/game_cast_party.c src/game/game_cast_travel.c src/game/game_cast_info.c src/game/game_combat.c src/game/game_cutscene.c src/game/game_cut_guard.c src/game/game_cut_sphinx.c src/game/game_cut_gate.c src/game/game_cut_reaper.c src/game/game_ending.c src/game/game_location.c src/game/game_location_frames.c src/game/game_inn.c src/game/game_creation.c src/game/game_menu.c src/game/game_menu_load.c src/game/game_window_text.c src/game/game_archive.c src/game/game_startup.c src/game/game_pairs.c src/game/game_newgame.c src/game/game_panel.c src/game/game_name_entry.c src/game/game_save.c src/game/game_shop.c src/game/game_item_options.c src/game/game_funds.c src/game/game_view.c src/game/game_details.c src/game/game_quests.c src/game/game_town.c src/game/game_tavern.c src/game/game_event_forms.c src/world/navigation.c src/events/event_dispatch.c src/world/gameplay_tick.c src/characters/party.c src/world/caller.c src/world/post_action.c src/core/timer.c src/graphics/scene.c src/audio/audio.c src/world/entities.c src/world/monster_update.c src/graphics/presentation.c src/graphics/scene_construct.c src/graphics/sprites.c src/world/minimap.c src/ui/interface.c src/world/world_entry.c src/combat/combat.c src/characters/items.c src/addon/addons.c src/addon/identify/known_items.c src/addon/identify/item_text.c src/addon/map_movement/map_pan.c src/addon/training_preview/ladder.c src/addon/damage_numbers/overlay.c src/addon/monster_difficulty/assess.c src/addon/monster_difficulty/overlay.c src/addon/spell_short_description/overlay.c src/addon/mouse_scroll/mouse_scroll.c src/addon/town_icons/overlay.c src/addon/character_details/overlay.c src/addon/character_details/texts.c src/addon/spell_list_sort/spell_list_sort.c src/addon/world_map/world_map.c src/addon/quest_journal/quest_journal.c src/addon/please_wait_skip/please_wait_skip.c src/addon/wizard_eye_plus/wizard_eye_plus.c src/addon/slot_file.c src/bugfix/bugfixes.c src/bugfix/spell_description/spell_description.c src/bugfix/quest_up_down/quest_up_down.c src/characters/town.c src/combat/ranged.c src/combat/projectile.c)
add_library(xeen::game ALIAS xeen_game)
set_target_properties(xeen_game PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED YES C_EXTENSIONS NO EXPORT_NAME game)
target_link_libraries(xeen_game PUBLIC xeen::event_resources xeen::event_ui xeen::character_record)
add_executable(xeen_native src/app/console.c)
set_target_properties(xeen_native PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
target_link_libraries(xeen_native PRIVATE xeen::game)
foreach(target IN ITEMS xeen_game xeen_native)
    if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion)
    endif()
endforeach()
install(TARGETS xeen_game EXPORT xeen_targets ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(TARGETS xeen_native RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES include/xeen/game.h include/xeen/navigation.h include/xeen/event_dispatch.h include/xeen/gameplay_tick.h include/xeen/combat.h include/xeen/ranged.h include/xeen/projectile.h DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/xeen)
install(FILES include/xeen/party.h include/xeen/caller.h include/xeen/post_action.h DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/xeen)

# Phase12 explicit BIOS/scene continuation; historical default boundaries remain.
install(FILES include/xeen/timer.h include/xeen/scene.h include/xeen/audio.h DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/xeen)

# Phase13 bounded runtime initialization, retaining all historical contracts.
install(FILES include/xeen/entities.h include/xeen/monster_update.h DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/xeen)

# Phase19 native presentation and bounded terrain construction.
install(FILES include/xeen/presentation.h include/xeen/windows.h include/xeen/scene_construct.h DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/xeen)

# Phase21 native input polling and resource-backed silent music.
target_sources(xeen_game PRIVATE src/ui/input_poll.c)

install(FILES include/xeen/input_poll.h include/xeen/world_entry.h DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/xeen)

# Phase22 authentic paired-save interpretation and startup.
target_sources(xeen_game PRIVATE src/save/save.c)
target_sources(xeen_game PRIVATE src/graphics/raster.c)
add_library(xeen_platform src/platform/platform.c src/platform/boss_screen.c)
target_link_libraries(xeen_platform PUBLIC xeen::game)
# Phase161: the window icons. src/icon/make_icons.py converts the owner's
# icon/wox_icon.bmp (multi-size .ico for the Windows resource, ARGB tables
# for the window backends) and draws the boss screen's calculator icon.
find_package(Python3 COMPONENTS Interpreter QUIET)
set(XEEN_ICON_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/icon/wox_icon.bmp")
set(XEEN_ICON_DIR "${CMAKE_CURRENT_BINARY_DIR}/icons")
if(Python3_Interpreter_FOUND AND EXISTS "${XEEN_ICON_SOURCE}")
    add_custom_command(OUTPUT "${XEEN_ICON_DIR}/xeen_icons.c" "${XEEN_ICON_DIR}/xeen_icons.rc"
            "${XEEN_ICON_DIR}/wox.ico" "${XEEN_ICON_DIR}/calc.ico"
        COMMAND ${Python3_EXECUTABLE} -B "${CMAKE_CURRENT_SOURCE_DIR}/src/icon/make_icons.py"
            "${XEEN_ICON_SOURCE}" "${XEEN_ICON_DIR}"
        DEPENDS "${XEEN_ICON_SOURCE}" "${CMAKE_CURRENT_SOURCE_DIR}/src/icon/make_icons.py"
        COMMENT "Converting icon/wox_icon.bmp into the window icons" VERBATIM)
    add_custom_target(xeen_icons DEPENDS "${XEEN_ICON_DIR}/xeen_icons.c" "${XEEN_ICON_DIR}/wox.ico" "${XEEN_ICON_DIR}/calc.ico")
    if(WIN32)
        # Windows loads the window icons from the .exe's resource (no tables).
        target_sources(xeen_platform PRIVATE src/icon/no_icons.c)
    else()
        target_sources(xeen_platform PRIVATE "${XEEN_ICON_DIR}/xeen_icons.c")
    endif()
    set(XEEN_HAVE_ICONS ON)
else()
    message(WARNING "Python 3 or icon/wox_icon.bmp missing: the executable gets no icon")
    target_sources(xeen_platform PRIVATE src/icon/no_icons.c)
    set(XEEN_HAVE_ICONS OFF)
endif()
set_target_properties(xeen_platform PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
if(WIN32)
    target_link_libraries(xeen_platform PRIVATE user32 gdi32 winmm)
    # Phase 335: the missing wox.cfg's question window and starting the setup tool (Windows only).
    target_sources(xeen_platform PRIVATE src/platform/ask_win32.c)
else()
    find_package(X11 QUIET)
    if(X11_FOUND)
        target_compile_definitions(xeen_platform PRIVATE XEEN_HAVE_X11)
        target_include_directories(xeen_platform PRIVATE ${X11_INCLUDE_DIR})
        target_link_libraries(xeen_platform PRIVATE ${X11_LIBRARIES})
    endif()
endif()
# The native host: explore.c (lifecycle, passes, feeds, evidence), its command line and its DOS file access.
# Phase 272: host_log.c is the window session's wox.log and fatal sequence (its stream capture reads each standard
# stream on its own thread: Win32 threads, elsewhere POSIX threads).
set(XEEN_HOST_SOURCES src/app/explore_options.c src/app/host_movie.c src/app/host_files.c src/app/host_iso.c src/app/host_evidence.c
    src/app/host_log.c src/app/stop_diagnostics.c src/app/host_startup.c)
# Phase 308: the host plays the XEEN.EXE movies (src/app/host_movie.c, the xeen_player library below).
# Phase 338: xeen_wox_version, the host's version number (--version, wox.log's session start).
set(XEEN_HOST_LIBS xeen_player xeen_wox_version)
if(NOT WIN32)
    set(THREADS_PREFER_PTHREAD_FLAG ON)
    find_package(Threads REQUIRED)
    list(APPEND XEEN_HOST_LIBS Threads::Threads)
endif()
add_executable(wox src/app/explore.c ${XEEN_HOST_SOURCES})
target_link_libraries(wox PRIVATE xeen_platform ${XEEN_HOST_LIBS})
if(WIN32)
    # Phase 272: the game is a GUI-subsystem program (-mwindows: no console window is created for it); its
    # entry point stays main(). Its messages go to wox.log; a redirection of stdout/stderr still receives them.
    set_target_properties(wox PROPERTIES WIN32_EXECUTABLE TRUE)
    # Phase 338: the version resource (FileVersion/ProductVersion from WOX_VERSION).
    enable_language(RC)
    target_sources(wox PRIVATE "${xeen_wox_version_RC}")
endif()
if(WIN32 AND XEEN_HAVE_ICONS)
    # Phase161: Explorer's, the taskbar's and Alt+Tab's icon of the .exe
    # (resource 1; resource 2 the boss screen's calculator).
    enable_language(RC)
    target_sources(wox PRIVATE "${XEEN_ICON_DIR}/xeen_icons.rc")
    set_source_files_properties("${XEEN_ICON_DIR}/xeen_icons.rc" PROPERTIES
        OBJECT_DEPENDS "${XEEN_ICON_DIR}/wox.ico;${XEEN_ICON_DIR}/calc.ico" GENERATED TRUE)
    add_dependencies(wox xeen_icons)
endif()
set_target_properties(wox PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
foreach(target IN ITEMS wox xeen_platform)
    if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion)
    endif()
endforeach()
install(TARGETS wox RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES include/xeen/save.h include/xeen/raster.h DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/xeen)

# Phase23 bounded original font/window service.
target_sources(xeen_game PRIVATE src/ui/text_window.c src/ui/modal.c src/ui/main_menu.c)
target_sources(xeen_game PRIVATE src/characters/treasure.c)
target_sources(xeen_game PRIVATE src/world/bash.c)
install(FILES include/xeen/text_window.h include/xeen/modal.h DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/xeen)
install(FILES include/xeen/treasure.h DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/xeen)
install(FILES include/xeen/bash.h DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/xeen)

# Phase132: wox.cfg and the native audio engine (the XEEN.EXE player's audio
# modules in src/audio, GPL-3.0-or-later). The engine needs SDL2 (audio device,
# threads); without it the frontend keeps the labelled silent backend.
add_library(xeen_wox_config STATIC src/platform/wox_config.c)
target_include_directories(xeen_wox_config PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
set_target_properties(xeen_wox_config PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED YES C_EXTENSIONS NO)

# Phase 331: the setup tool (owner-approved host tool, not original behaviour): `setup` (Windows setup.exe) beside wox
# writes wox.cfg and the game's XEEN.CFG. Its core (src/setup/setup_model.c) has no toolkit; the window is native
# Win32 on Windows and GTK 3 elsewhere (a build dependency of setup only: without GTK 3 development files the target
# is skipped with a message; wox never needs GTK).
add_library(xeen_setup_model STATIC src/setup/setup_model.c src/setup/setup_roms.c)
target_include_directories(xeen_setup_model PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(xeen_setup_model PUBLIC xeen_wox_config)
set_target_properties(xeen_setup_model PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
set(XEEN_SETUP_TARGET OFF)
if(WIN32)
    enable_language(RC)
    add_executable(setup WIN32 src/setup/setup_win32.c src/setup/setup.rc "${xeen_setup_version_RC}")
    set_source_files_properties(src/setup/setup.rc PROPERTIES OBJECT_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/setup/setup.manifest)
    target_link_libraries(setup PRIVATE xeen_setup_model comctl32 ole32 uuid uxtheme shell32 winhttp)
    # Phase 335: setup.exe was 987 KB, two thirds of it the symbol table and the static CRT's DWARF debug information.
    # A settings window needs no speed: its code is built for size, unused functions are dropped, and the
    # executable is stripped outside Debug (about 300 KB, of which the icon is about 185 KB).
    if(MINGW)
        target_compile_options(setup PRIVATE -Os -ffunction-sections -fdata-sections)
        target_compile_options(xeen_setup_model PRIVATE -Os -ffunction-sections -fdata-sections)
        target_link_options(setup PRIVATE -Wl,--gc-sections $<$<NOT:$<CONFIG:Debug>>:-s>)
    endif()
    # Phase 333: the window's and Explorer's icon, resource 1, from the owner's icon/setup_icon.bmp.
    set(XEEN_SETUP_ICON_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/icon/setup_icon.bmp")
    if(Python3_Interpreter_FOUND AND EXISTS "${XEEN_SETUP_ICON_SOURCE}")
        add_custom_command(OUTPUT "${XEEN_ICON_DIR}/setup.ico" "${XEEN_ICON_DIR}/setup.rc"
            COMMAND ${Python3_EXECUTABLE} -B "${CMAKE_CURRENT_SOURCE_DIR}/src/icon/make_icons.py" --single
                "${XEEN_SETUP_ICON_SOURCE}" "${XEEN_ICON_DIR}" setup
            DEPENDS "${XEEN_SETUP_ICON_SOURCE}" "${CMAKE_CURRENT_SOURCE_DIR}/src/icon/make_icons.py"
            COMMENT "Converting icon/setup_icon.bmp into the setup tool's icon" VERBATIM)
        add_custom_target(xeen_setup_icon DEPENDS "${XEEN_ICON_DIR}/setup.ico" "${XEEN_ICON_DIR}/setup.rc")
        target_sources(setup PRIVATE "${XEEN_ICON_DIR}/setup.rc")
        set_source_files_properties("${XEEN_ICON_DIR}/setup.rc" PROPERTIES
            OBJECT_DEPENDS "${XEEN_ICON_DIR}/setup.ico" GENERATED TRUE)
        add_dependencies(setup xeen_setup_icon)
    else()
        message(WARNING "Python 3 or icon/setup_icon.bmp missing: setup.exe gets no icon")
    endif()
    if(MINGW)
        target_link_options(setup PRIVATE -static)
    endif()
    set(XEEN_SETUP_TARGET ON)
else()
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(XEEN_GTK3 QUIET IMPORTED_TARGET gtk+-3.0)
    endif()
    if(XEEN_GTK3_FOUND)
        add_executable(setup src/setup/setup_gtk.c)
        target_link_libraries(setup PRIVATE xeen_setup_model PkgConfig::XEEN_GTK3)
        set(XEEN_SETUP_TARGET ON)
    else()
        message(STATUS "setup: GTK 3 development files (gtk+-3.0) not found; the setup tool is not built")
    endif()
endif()
if(XEEN_SETUP_TARGET)
    target_link_libraries(setup PRIVATE xeen_setup_version) # Phase 338: XEEN_SETUP_VERSION (the main window's title)
    set_target_properties(setup PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
    install(TARGETS setup RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()
foreach(target IN ITEMS xeen_setup_model setup)
    if(TARGET ${target} AND CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion)
    endif()
endforeach()
# Phase 308: the parts of the reused XEEN.EXE modules that need no SDL (VOC decode, cc_read over
# xeen_archive, UTF-8 paths), shared by the audio engine and the movie player.
add_library(xeen_audio_common STATIC src/audio/voc.c src/audio/file_io.c src/audio/audio_archive.c)
target_include_directories(xeen_audio_common PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(xeen_audio_common PUBLIC xeen::resources)
set_target_properties(xeen_audio_common PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
if(NOT WIN32)
    target_compile_definitions(xeen_audio_common PRIVATE _POSIX_C_SOURCE=200809L)
endif()
# Phase 308: the XEEN.EXE movie player (src/player/, migrated from the separate reconstruction;
# include/xeen/movie.h). It draws through the game's window and plays through its audio sink.
file(GLOB XEEN_PLAYER_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/player/*.c)
add_library(xeen_player STATIC ${XEEN_PLAYER_SOURCES})
target_include_directories(xeen_player PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src/player ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(xeen_player PUBLIC xeen_audio_common)
if(NOT WIN32)
    target_link_libraries(xeen_player PUBLIC m)
    target_compile_definitions(xeen_player PRIVATE _POSIX_C_SOURCE=200809L)
endif()
set_target_properties(xeen_player PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(xeen_player PRIVATE -Wall -Wextra -Wpedantic)
endif()
option(XEEN_AUDIO "Build the native audio engine (needs SDL2)" ON)
set(XEEN_AUDIO_DEPS "${CMAKE_CURRENT_SOURCE_DIR}/deps" CACHE PATH
    "The pinned audio dependency trees (tools/deps/fetch_deps.sh)")
find_path(SDL2_INCLUDE_DIR SDL.h PATH_SUFFIXES SDL2)
find_library(SDL2_LIBRARY NAMES SDL2-2.0 libSDL2-2.0.so.0 SDL2)
if(XEEN_AUDIO AND SDL2_INCLUDE_DIR AND SDL2_LIBRARY)
    add_library(xeen_audio STATIC src/audio/audio_engine.c src/audio/music.c src/audio/synth.c
        src/audio/synth_stream.c src/audio/mt32.c src/audio/sc55_loader.c src/audio/sbpro_loader.c
        src/audio/midi_output.c
        # Phase134: the CD voice rips; stb_vorbis v1.22 (public domain / MIT, nothings/stb
        # 1ee679ca2ef753a528db5ba6801e1067b40481b8), vendored unmodified.
        src/audio/cd_stream.c src/audio/stb_vorbis.c)
    target_include_directories(xeen_audio PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include ${SDL2_INCLUDE_DIR}
        PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_link_libraries(xeen_audio PUBLIC xeen::game xeen_wox_config xeen_audio_common ${SDL2_LIBRARY} ${CMAKE_DL_LIBS})
    # Phase 335 (owner request): Windows links SDL2 statically into wox.exe (tools/windows/package.sh): the system
    # libraries the static SDL2 needs (its sdl2.pc Libs.private); only the ones it references are imported.
    if(WIN32 AND SDL2_LIBRARY MATCHES "\\.a$" AND NOT SDL2_LIBRARY MATCHES "\\.dll\\.a$")
        target_link_libraries(xeen_audio PUBLIC user32 gdi32 winmm imm32 ole32 oleaut32 version uuid advapi32 setupapi
                              shell32)
    elseif(NOT WIN32 AND SDL2_LIBRARY MATCHES "\\.a$")
        # Linux (tools/linux/package.sh): the static SDL2 loads its ALSA/PulseAudio backends at run time
        find_package(Threads REQUIRED)
        target_link_libraries(xeen_audio PUBLIC Threads::Threads ${CMAKE_DL_LIBS})
    endif()
    if(NOT WIN32)
        target_link_libraries(xeen_audio PUBLIC m)   # stb_vorbis
    endif()
    set_target_properties(xeen_audio PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
    target_compile_definitions(xeen_audio PRIVATE XEEN_HAVE_SC55 XEEN_HAVE_SBPRO)
    if(WIN32)
        target_link_libraries(xeen_audio PUBLIC winmm)
    else()
        target_compile_definitions(xeen_audio PRIVATE _POSIX_C_SOURCE=200809L)
    endif()
    # Munt is loaded at run time (the player's mt32.c: xeen_mt32.dll beside the
    # executable on Windows, libmt32emu.so.2 on Linux); only its C headers are needed
    # to build. A pinned Linux build's path is tried first when it exists. On Windows,
    # src/audio/MT32.cmake builds xeen_mt32.dll from the pinned Munt source and the
    # executable uses that source's C headers.
    set(XEEN_MT32_SOURCE_DIR "${XEEN_AUDIO_DEPS}/mt32emu/munt-libmt32emu_2_8_3/mt32emu" CACHE PATH
        "Pinned Munt (libmt32emu 2.8.3) source")
    if(WIN32 AND EXISTS "${XEEN_MT32_SOURCE_DIR}/src/c_interface/c_interface.cpp")
        enable_language(CXX)
        include(src/audio/MT32.cmake)
        add_dependencies(wox xeen_mt32)
        set(XEEN_MT32_INCLUDE "${XEEN_MT32_GENERATED_INCLUDE}")
    elseif(WIN32)
        set(XEEN_MT32_INCLUDE "${XEEN_AUDIO_DEPS}/windows-munt/include" CACHE PATH "Munt C headers")
    else()
        set(XEEN_MT32_INCLUDE "${XEEN_AUDIO_DEPS}/mt32emu-linux/include" CACHE PATH "Munt C headers")
    endif()
    # An absolute libmt32emu path to try first (developer builds only; empty: none is compiled in, and Munt is found
    # beside wox or through the system's library search, as in the packages).
    set(XEEN_MT32_LIBRARY "" CACHE FILEPATH "libmt32emu to load first")
    if(EXISTS "${XEEN_MT32_INCLUDE}/mt32emu/c_interface/c_types.h")
        target_compile_definitions(xeen_audio PRIVATE XEEN_HAVE_MT32)
        target_include_directories(xeen_audio PRIVATE "${XEEN_MT32_INCLUDE}")
        if(NOT WIN32 AND XEEN_MT32_LIBRARY AND EXISTS "${XEEN_MT32_LIBRARY}")
            target_compile_definitions(xeen_audio PRIVATE XEEN_MT32_LIBRARY_PATH="${XEEN_MT32_LIBRARY}")
        endif()
    endif()
    target_link_libraries(wox PRIVATE xeen_audio)
    target_compile_definitions(wox PRIVATE XEEN_HAVE_AUDIO)
    # The optional backends are the player's shared libraries, built from its pinned trees.
    set(XEEN_YMFM_SOURCE_DIR "${XEEN_AUDIO_DEPS}/ymfm-src" CACHE PATH "Pinned standalone ymfm source")
    if(EXISTS "${XEEN_YMFM_SOURCE_DIR}/src/ymfm_opl.cpp")
        enable_language(CXX)
        add_library(xeen_sbpro SHARED src/audio/sbpro_sequence.c src/audio/fm1_sequence.c src/audio/sbpro.cpp
            "${XEEN_YMFM_SOURCE_DIR}/src/ymfm_opl.cpp" "${XEEN_YMFM_SOURCE_DIR}/src/ymfm_adpcm.cpp"
            "${XEEN_YMFM_SOURCE_DIR}/src/ymfm_pcm.cpp")
        target_include_directories(xeen_sbpro PRIVATE src/audio "${XEEN_YMFM_SOURCE_DIR}/src")
        target_compile_features(xeen_sbpro PRIVATE cxx_std_17)
        target_compile_definitions(xeen_sbpro PRIVATE XEEN_SBPRO_BUILD)
        set_target_properties(xeen_sbpro PROPERTIES CXX_VISIBILITY_PRESET hidden C_VISIBILITY_PRESET hidden
            VISIBILITY_INLINES_HIDDEN YES VERSION 1.0.0 SOVERSION 1 C_STANDARD 11)
        if(WIN32)
            set_target_properties(xeen_sbpro PROPERTIES PREFIX "")
        endif()
        # The XEEN.EXE player's small build (its cmake/SBPro.cmake, "Reduce Windows DLLs"):
        # -Os, LTO and section collection; on MinGW its own static GCC runtime, so this
        # optional DLL needs only KERNEL32 and msvcrt and exports none of the runtime's symbols.
        # (Before this name, wox_sbpro.)
        if(NOT MSVC)
            target_compile_options(xeen_sbpro PRIVATE -Os -ffunction-sections -fdata-sections)
            target_link_options(xeen_sbpro PRIVATE -Wl,--gc-sections)
        endif()
        if(MINGW)
            include(CheckIPOSupported)
            check_ipo_supported(RESULT sbpro_ipo LANGUAGES C CXX OUTPUT sbpro_ipo_error)
            if(NOT sbpro_ipo)
                message(FATAL_ERROR "SB Pro size build requires IPO: ${sbpro_ipo_error}")
            endif()
            set_property(TARGET xeen_sbpro PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
            target_link_options(xeen_sbpro PRIVATE -static-libgcc -static-libstdc++
                -Wl,--exclude-libs,ALL,--no-insert-timestamp)
        endif()
        add_dependencies(wox xeen_sbpro)
    endif()
    set(XEEN_SC55_SOURCE_DIR "${XEEN_AUDIO_DEPS}/nuked-sc55-src" CACHE PATH "Pinned Nuked-SC55 source")
    option(XEEN_SC55 "Build the optional Nuked-SC55 backend library" ON)
    if(XEEN_SC55 AND EXISTS "${XEEN_SC55_SOURCE_DIR}/src/backend/emu.cpp")
        include(src/audio/SC55.cmake)
        add_dependencies(wox xeen_sc55)
    endif()
endif()

# The public-safe tests (no original game files, ROMs or private tooling needed): tests/tests.cmake.
if(BUILD_TESTING)
    include(tests/tests.cmake)
endif()

# Phase 284: the test-only functions (tools/refactor/linkage_census.py: called by no shipped program, only by tests)
# live in src/test_support/, one file per module they came from, built into xeen_test_support instead of the game's
# libraries. Every executable except the programs below links it (the test programs, probes and fixtures, the local
# research runners); the game links only what it uses, so its executable no longer carries them. The library is
# installed with the others: the installed headers keep declaring these functions.
add_library(xeen_test_support STATIC
    src/test_support/audio_test_support.c src/test_support/character_record_test_support.c
    src/test_support/entities_test_support.c src/test_support/event_records_test_support.c
    src/test_support/event_ui_test_support.c src/test_support/game_lifecycle_test_support.c
    src/test_support/game_resources_test_support.c src/test_support/movement_test_support.c
    src/test_support/post_action_test_support.c src/test_support/resources_test_support.c
    src/test_support/text_window_test_support.c src/test_support/treasure_test_support.c)
add_library(xeen::test_support ALIAS xeen_test_support)
set_target_properties(xeen_test_support PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED YES C_EXTENSIONS NO
    EXPORT_NAME test_support)
target_link_libraries(xeen_test_support PUBLIC xeen::game)
install(TARGETS xeen_test_support EXPORT xeen_targets ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
set(XEEN_SHIPPED_PROGRAMS wox xeen_native)   # the game and the developer console
get_property(xeen_all_targets DIRECTORY PROPERTY BUILDSYSTEM_TARGETS)
foreach(xeen_target IN LISTS xeen_all_targets)
    get_target_property(xeen_target_type ${xeen_target} TYPE)
    if(xeen_target_type STREQUAL "EXECUTABLE" AND NOT xeen_target IN_LIST XEEN_SHIPPED_PROGRAMS)
        target_link_libraries(${xeen_target} PRIVATE xeen::test_support)
    endif()
endforeach()

