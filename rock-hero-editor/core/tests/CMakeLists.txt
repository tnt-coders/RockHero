# Headless editor workflow tests exercise controller policy without constructing JUCE widgets.
find_package(Catch2 3 REQUIRED)
include(Catch)

# Keep controller, settings, and package workflow coverage in the headless core test target.
add_executable(
    rock_hero_editor_core_tests
    test_audio_device_settings_controller.cpp test_audio_device_status_text.cpp
    test_editor_controller.cpp test_input_calibration_controller.cpp test_project.cpp
    test_editor_settings.cpp)

# Link JUCE core for project package fixtures while keeping tests on the editor-core boundary.
target_link_libraries(
    rock_hero_editor_core_tests
    PRIVATE rock_hero::editor::core rock_hero::build_policy rock_hero::juce_audio_devices
            rock_hero::juce_core Catch2::Catch2WithMain)

# Keep persisted editor-settings files inside the build tree.
target_compile_definitions(rock_hero_editor_core_tests
                           PRIVATE TEST_SETTINGS_DIR="${CMAKE_CURRENT_BINARY_DIR}")

# Register the discovered Catch2 cases with CTest and forward Catch tags as CTest labels.
catch_discover_tests(rock_hero_editor_core_tests ADD_TAGS_AS_LABELS REPORTER JSON)
