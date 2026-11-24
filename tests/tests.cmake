# The public-safe tests: headless unit and contract tests that need no original game files, ROMs, CD rips or
# private research tooling (included from CMakeLists.txt when BUILD_TESTING is ON). They do need the generated
# program data (src/data), like every target linking xeen::game. Run: ctest --test-dir BUILD --output-on-failure
#
# The expectations in these tests were established against the original program during development; the evidence
# itself is not part of this source tree. Test names keep the development history's phase numbers.

function(xeen_public_test name exe source)
    cmake_parse_arguments(T "" "" "LIBS;ARGS;INCLUDES;EXTRA_SOURCES" ${ARGN})
    if(NOT TARGET ${exe})
        add_executable(${exe} "${CMAKE_CURRENT_SOURCE_DIR}/tests/${source}" ${T_EXTRA_SOURCES})
        target_link_libraries(${exe} PRIVATE ${T_LIBS})
        if(T_INCLUDES)
            target_include_directories(${exe} PRIVATE ${T_INCLUDES})
        endif()
        set_target_properties(${exe} PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
    endif()
    add_test(NAME ${name} COMMAND ${exe} ${T_ARGS})
endfunction()

set(XEEN_TEST_SRC "${CMAKE_CURRENT_SOURCE_DIR}/src")

# Character records, movement, resources, events.
foreach(contract IN ITEMS 2c909 2ab96 46364 3cf35)
    xeen_public_test(phase1_${contract} xeen_character_record_tests test_phase1.c LIBS xeen::character_record
                     ARGS ${contract})
endforeach()
xeen_public_test(phase2_movement xeen_movement_tests test_movement.c LIBS xeen::movement)
xeen_public_test(phase5b_scene_prefix xeen_scene_prefix_tests test_scene_prefix.c LIBS xeen::movement)
xeen_public_test(phase3_resources xeen_resource_tests test_resources.c LIBS xeen::resources)
xeen_public_test(phase5c_events xeen_event_tests test_events.c LIBS xeen::events)
xeen_public_test(phase6_event_execution xeen_event_exec_tests test_event_exec.c LIBS xeen::events)
xeen_public_test(phase7_event_text xeen_event_text_tests test_event_text.c LIBS xeen::event_resources)
xeen_public_test(phase7_event_operations xeen_event_expanded_tests test_event_expanded.c LIBS xeen::event_resources)
xeen_public_test(phase8_event_ui xeen_event_ui_tests test_event_ui.c
                 LIBS xeen::event_ui xeen::event_resources xeen::game)
xeen_public_test(phase125_mirror_metadata xeen_phase125_mirror_tests test_phase125_mirror.c LIBS xeen::movement)

# The game session's rules and contracts (synthetic states, no game files).
foreach(spec IN ITEMS
        "phase330_space_gate_units|xeen_space_gate_tests|test_space_gate.c|units"
        "phase10_dispatcher_boundaries|xeen_lifecycle_tests|test_lifecycle.c|"
        "phase11_caller_boundaries|xeen_caller_tests|test_caller.c|"
        "phase12_timer_contract|xeen_timer_tests|test_timer.c|"
        "phase12_scene_boundary|xeen_scene_tests|test_scene.c|"
        "phase13_entity_validation|xeen_entities_tests|test_entities.c|"
        "phase15_outdoor_contract|xeen_outdoor_tests|test_outdoor.c|"
        "phase19_presentation|xeen_presentation_tests|test_presentation.c|"
        "phase19_restoration|xeen_scene_restore_tests|test_scene_restore.c|"
        "phase20_contracts|xeen_phase20_scene_tests|test_phase20_scene.c|units"
        "phase21_monster_update|xeen_input_release_tests|test_input_release.c|"
        "phase273_monster_recovery|xeen_phase273_monster_recovery_tests|test_phase273_monster_recovery.c|"
        "phase276_click_control|xeen_phase276_click_control_tests|test_phase276_click_control.c|"
        "phase302_view_occlusion|xeen_phase302_view_occlusion|test_phase302_view_occlusion.c|"
        "phase303_scene_records|xeen_phase303_scene_records|test_phase303_scene_records.c|"
        "phase24_opcode2|xeen_opcode2_tests|test_opcode2.c|"
        "phase25_condition20|xeen_condition20_tests|test_condition20.c|"
        "phase26_event_semantics|xeen_phase26_event_tests|test_phase26_event.c|"
        "phase29_alter_event|xeen_alter_event_tests|test_alter_event.c|"
        "phase93_if_conditions|xeen_phase93_if_tests|test_phase93_if_conditions.c|"
        "phase94_monster_rows|xeen_phase94_monster_row_tests|test_phase94_monster_rows.c|"
        "phase33_combat_contracts|xeen_phase33_combat_tests|test_phase33_combat.c|"
        "phase34_combat_contracts|xeen_phase34_combat_tests|test_phase34_combat.c|"
        "phase35_combat_contracts|xeen_phase35_combat_tests|test_phase35_combat.c|"
        "phase36_monster_action_contracts|xeen_phase35_combat_tests|test_phase35_combat.c|"
        "phase37_combat_contracts|xeen_phase37_combat_tests|test_phase37_combat.c|"
        "phase39_combat_contracts|xeen_phase39_combat_tests|test_phase39_combat.c|"
        "phase40_combat_rules|xeen_phase40_combat_tests|test_phase40_combat.c|"
        "phase41_treasure_contracts|xeen_phase41_treasure_tests|test_phase41_treasure.c|"
        "phase41b_bash_contracts|xeen_phase41b_bash_tests|test_phase41b_bash.c|"
        "phase43_collection_contracts|xeen_phase43_collection_tests|test_phase43_collection.c|"
        "phase44_rng_contract|xeen_phase44_rng_tests|test_phase44_rng.c|"
        "phase47_outdoor_combat_rules|xeen_phase47_outdoor_tests|test_phase47_outdoor.c|"
        "phase48_outdoor_multiround_rules|xeen_phase48_multiround_tests|test_phase48_outdoor_multiround.c|"
        "phase49_shoot_rules|xeen_phase49_shoot_tests|test_phase49_shoot_rules.c|"
        "phase50_treasure_rules|xeen_phase50_treasure_tests|test_phase50_treasure_rules.c|"
        "phase51_condition_rules|xeen_phase51_condition_tests|test_phase51_condition_rules.c|"
        "phase52_time_rules|xeen_phase52_time_tests|test_phase52_time_rules.c|"
        "phase53_rest_rules|xeen_phase53_rest_tests|test_phase53_rest_rules.c|"
        "phase54_rules|xeen_phase54_rules_tests|test_phase54_rules.c|"
        "phase73_darkside_death_rules|xeen_phase73_darkside_death_tests|test_phase73_darkside_death.c|"
        "phase126_presentation_rules|xeen_phase126_presentation_tests|test_phase126_presentation.c|")
    string(REPLACE "|" ";" spec "${spec}")
    list(GET spec 0 t_name)
    list(GET spec 1 t_exe)
    list(GET spec 2 t_src)
    list(GET spec 3 t_arg)
    xeen_public_test(${t_name} ${t_exe} ${t_src} LIBS xeen::game ARGS ${t_arg})
endforeach()

# The platform layer (headless: no window is opened).
foreach(spec IN ITEMS
        "phase22_platform|xeen_platform_tests|test_platform.c"
        "phase79_mouse|xeen_phase79_mouse_tests|test_phase79_mouse.c"
        "phase137_ui|xeen_phase137_ui_tests|test_phase137_ui.c"
        "phase166_title_quit|xeen_phase166_title_quit_tests|test_phase166_title_quit.c"
        "phase167_menu_quit|xeen_phase167_menu_quit_tests|test_phase167_menu_quit.c"
        "phase269_keyboard|xeen_phase269_keyboard_tests|test_phase269_keyboard.c")
    string(REPLACE "|" ";" spec "${spec}")
    list(GET spec 0 t_name)
    list(GET spec 1 t_exe)
    list(GET spec 2 t_src)
    xeen_public_test(${t_name} ${t_exe} ${t_src} LIBS xeen_platform)
endforeach()
xeen_public_test(phase161_boss_screen xeen_phase161_boss_tests test_phase161_boss.c LIBS xeen_platform
                 ARGS "${XEEN_ICON_SOURCE}")
xeen_public_test(phase175_cursor xeen_phase175_cursor_tests test_phase175_cursor.c LIBS xeen_platform xeen::game)
if(UNIX AND NOT APPLE)
    # src/platform/platform.c's Win32 window code against a logging mock of the Win32 API (tests/win32_mock).
    find_package(Threads REQUIRED)
    add_executable(xeen_phase298_win32_window_tests tests/test_phase298_win32_window.c)
    target_include_directories(xeen_phase298_win32_window_tests SYSTEM BEFORE PRIVATE
                               ${CMAKE_CURRENT_SOURCE_DIR}/tests/win32_mock)
    target_include_directories(xeen_phase298_win32_window_tests PRIVATE ${XEEN_TEST_SRC})
    target_link_libraries(xeen_phase298_win32_window_tests PRIVATE xeen_platform Threads::Threads)
    set_target_properties(xeen_phase298_win32_window_tests PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED YES
                          C_EXTENSIONS NO)
    add_test(NAME phase298_win32_window COMMAND xeen_phase298_win32_window_tests 300)
    set_tests_properties(phase298_win32_window PROPERTIES TIMEOUT 60)
endif()
if(X11_FOUND)
    # platform.c's X11 paint and waits under a logging mock Xlib (no display needed).
    foreach(t IN ITEMS phase334_x11_paint phase335_x11_boss_wait)
        add_executable(xeen_${t}_tests tests/test_${t}.c)
        target_compile_definitions(xeen_${t}_tests PRIVATE XEEN_HAVE_X11)
        target_include_directories(xeen_${t}_tests PRIVATE ${X11_INCLUDE_DIR} ${XEEN_TEST_SRC}
                                   ${CMAKE_CURRENT_SOURCE_DIR}/tests)
        target_link_libraries(xeen_${t}_tests PRIVATE xeen_platform)
        set_target_properties(xeen_${t}_tests PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
    endforeach()
    add_test(NAME phase334_x11_paint COMMAND xeen_phase334_x11_paint_tests
             "${CMAKE_CURRENT_SOURCE_DIR}/tests/data/phase334_x11_paint.txt")
    add_test(NAME phase335_x11_boss_wait COMMAND xeen_phase335_x11_boss_wait_tests)
    set_tests_properties(phase334_x11_paint phase335_x11_boss_wait PROPERTIES TIMEOUT 60)
endif()

# wox.cfg, the setup tool's model and the version numbers.
xeen_public_test(phase132_wox_config xeen_phase132_config_tests test_phase132_config.c LIBS xeen_wox_config)
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/phase308_wox_config")
xeen_public_test(phase308_wox_config xeen_phase308_config_tests test_phase308_config.c LIBS xeen_wox_config
                 ARGS "${CMAKE_CURRENT_BINARY_DIR}/phase308_wox_config")
set_tests_properties(phase132_wox_config phase308_wox_config PROPERTIES TIMEOUT 30)
file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/phase331_setup)
xeen_public_test(phase331_setup xeen_phase331_setup_tests test_phase331_setup.c LIBS xeen_setup_model)
set_tests_properties(phase331_setup PROPERTIES TIMEOUT 60 WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/phase331_setup)
xeen_public_test(phase338_setup_title xeen_phase338_setup_title_tests test_phase338_setup_title.c
                 LIBS xeen_setup_version INCLUDES ${XEEN_TEST_SRC} ARGS "${SETUP_VERSION}")
find_package(Python3 COMPONENTS Interpreter QUIET)
if(Python3_Interpreter_FOUND AND NOT WIN32)
    set(XEEN_P338_SETUP "")
    if(XEEN_SETUP_TARGET)
        set(XEEN_P338_SETUP $<TARGET_FILE:setup>)
    endif()
    add_test(NAME phase338_versions COMMAND ${Python3_EXECUTABLE} -B
             "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_phase338_versions.py"
             $<TARGET_FILE:wox> "${CMAKE_CURRENT_BINARY_DIR}" "${XEEN_P338_SETUP}")
    set_tests_properties(phase338_versions PROPERTIES TIMEOUT 120
        ENVIRONMENT "XEEN_WOX_VERSION=${WOX_VERSION};XEEN_SETUP_VERSION=${SETUP_VERSION};XEEN_CMAKE=${CMAKE_COMMAND}")
endif()

# The audio engine (only when it is built: SDL2 found).
if(TARGET xeen_audio)
    xeen_public_test(phase286_voice_drivers xeen_phase286_drivers_tests test_phase286_drivers.c LIBS xeen_audio
                     INCLUDES ${XEEN_TEST_SRC} ARGS voice "${CMAKE_CURRENT_SOURCE_DIR}/tests/phase286_voice_cases.txt")
    xeen_public_test(phase287_voice_block xeen_phase286_drivers_tests test_phase286_drivers.c
                     ARGS block "${CMAKE_CURRENT_SOURCE_DIR}/tests/phase287_block_cases.txt")
    set_tests_properties(phase286_voice_drivers phase287_voice_block PROPERTIES TIMEOUT 30)
    xeen_public_test(phase290_startup_lifecycle xeen_phase290_startup_tests test_phase290_startup.c
                     LIBS xeen_audio xeen_platform INCLUDES ${XEEN_TEST_SRC}
                     EXTRA_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/app/host_startup.c" ARGS lifecycle)
    set_tests_properties(phase290_startup_lifecycle PROPERTIES TIMEOUT 30)
    if(CMAKE_C_COMPILER_ID STREQUAL "GNU" AND NOT WIN32 AND NOT APPLE)
        # xeen_audio_engine_open's failure and cleanup paths (GNU ld --wrap around calloc and the SDL calls).
        xeen_public_test(phase330_audio_open xeen_phase330_audio_open_tests test_phase330_audio_open.c LIBS xeen_audio)
        target_link_options(xeen_phase330_audio_open_tests PRIVATE
            "LINKER:--wrap=calloc,--wrap=SDL_InitSubSystem,--wrap=SDL_QuitSubSystem,--wrap=SDL_OpenAudioDevice"
            "LINKER:--wrap=SDL_CloseAudioDevice,--wrap=SDL_CreateMutex,--wrap=SDL_DestroyMutex"
            "LINKER:--wrap=SDL_CreateThread,--wrap=SDL_WaitThread")
    endif()
endif()

# These tests read the real program data (the original game's data segment): CI, which builds with the placeholder
# (XEEN_PROGRAM_DATA_STUB), skips them with `ctest -LE needs-program-data`.
set_tests_properties(phase8_event_ui phase24_opcode2 phase276_click_control phase302_view_occlusion
                     phase303_scene_records PROPERTIES LABELS needs-program-data)
