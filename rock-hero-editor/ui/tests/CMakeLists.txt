# UI tests exercise synchronous JUCE widget wiring without launching the editor app.
find_package(Catch2 3 REQUIRED)
include(Catch)

# Test-only helpers for editor UI tests. The target stays opt-in so production editor UI does not
# acquire testing include paths or helper dependencies.
add_library(rock_hero_editor_ui_testing INTERFACE)
add_library(rock_hero::editor::ui_testing ALIAS rock_hero_editor_ui_testing)

# The editor-view harness intentionally constructs private UI components. Tests opt in through
# this testing target rather than adding a second ad hoc support include path.
target_include_directories(
    rock_hero_editor_ui_testing INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/include"
                                          "${CMAKE_CURRENT_SOURCE_DIR}/../src")

target_link_libraries(rock_hero_editor_ui_testing INTERFACE rock_hero::editor::ui Catch2::Catch2)

# Keep focused editor UI coverage in one target because the test surface is small.
add_executable(
    rock_hero_editor_ui_tests
    test_arrangement_view.cpp
    test_audio_device_settings_view.cpp
    test_editor.cpp
    test_editor_view_audio_controls.cpp
    test_editor_view_busy_overlay.cpp
    test_editor_view_layout.cpp
    test_tone_automation_lanes_view.cpp
    test_editor_view_signal_chain.cpp
    test_editor_view_state.cpp
    test_editor_view_timeline.cpp
    test_grid_spacing_selector.cpp
    test_input_calibration_window.cpp
    test_plugin_browser_window.cpp
    test_signal_chain_block_layout.cpp
    test_tab_view.cpp
    test_tone_track_view.cpp
    test_transport_controls.cpp)

# Let focused smoke tests construct private editor view types without making them public API.
target_include_directories(rock_hero_editor_ui_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../src")

# Link only the editor UI module and Catch2; concrete app composition stays outside this target.
target_link_libraries(
    rock_hero_editor_ui_tests
    PRIVATE rock_hero::editor::ui rock_hero::common::audio_testing rock_hero::editor::core_testing
            rock_hero::editor::ui_testing rock_hero::build_policy Catch2::Catch2WithMain)

# Register the discovered Catch2 cases with CTest and forward Catch tags as CTest labels.
catch_discover_tests(rock_hero_editor_ui_tests ADD_TAGS_AS_LABELS)
