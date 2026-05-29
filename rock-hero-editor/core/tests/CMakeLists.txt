# Headless editor workflow tests exercise controller policy without constructing JUCE widgets.
find_package(Catch2 3 REQUIRED)
include(Catch)

# Test-only helpers for editor-core contracts. Downstream tests opt in explicitly instead of
# reaching into this test directory by relative include path.
add_library(rock_hero_editor_core_testing INTERFACE)
add_library(rock_hero::editor::core_testing ALIAS rock_hero_editor_core_testing)

target_include_directories(rock_hero_editor_core_testing
                           INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/include")

target_link_libraries(rock_hero_editor_core_testing INTERFACE rock_hero::editor::core
                                                              Catch2::Catch2)

# Keep controller, settings, and package workflow coverage in the headless core test target.
add_executable(
    rock_hero_editor_core_tests
    test_audio_device_settings_controller.cpp
    test_audio_device_status_text.cpp
    test_editor_controller_busy.cpp
    test_editor_controller_input_calibration.cpp
    test_editor_controller_output_gain.cpp
    test_editor_controller_plugins.cpp
    test_editor_controller_project_lifecycle.cpp
    test_editor_controller_restore.cpp
    test_editor_controller_state.cpp
    test_editor_controller_transport.cpp
    test_project.cpp
    test_editor_settings.cpp)

# Project tests that exercise concrete importers need access to private library headers.
target_include_directories(rock_hero_editor_core_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../src")

# Link JUCE core for project package fixtures while keeping tests on the editor-core boundary.
target_link_libraries(
    rock_hero_editor_core_tests
    PRIVATE rock_hero::editor::core
            rock_hero::build_policy
            rock_hero::juce_audio_devices
            rock_hero::juce_core
            rock_hero::common::audio_testing
            rock_hero::editor::core_testing
            Catch2::Catch2WithMain)

# Keep persisted editor-settings files inside the build tree.
target_compile_definitions(rock_hero_editor_core_tests
                           PRIVATE TEST_SETTINGS_DIR="${CMAKE_CURRENT_BINARY_DIR}")

# Register the discovered Catch2 cases with CTest and forward Catch tags as CTest labels.
catch_discover_tests(rock_hero_editor_core_tests ADD_TAGS_AS_LABELS REPORTER JSON)
