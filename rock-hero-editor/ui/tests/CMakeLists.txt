# UI tests exercise synchronous JUCE widget wiring without launching the editor app.
find_package(Catch2 3 REQUIRED)
include(Catch)

# Keep focused editor UI coverage in one target because the test surface is small.
add_executable(
    rock_hero_editor_ui_tests
    test_arrangement_view.cpp test_audio_device_settings_view.cpp test_editor.cpp
    test_editor_view.cpp test_plugin_browser_window.cpp test_transport_controls.cpp)

# Let focused smoke tests construct private editor view types without making them public API.
target_include_directories(rock_hero_editor_ui_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../src")

# Link only the editor UI module and Catch2; concrete app composition stays outside this target.
target_link_libraries(rock_hero_editor_ui_tests
                      PRIVATE rock_hero::editor::ui rock_hero::build_policy Catch2::Catch2WithMain)

# Register the discovered Catch2 cases with CTest and forward Catch tags as CTest labels.
catch_discover_tests(rock_hero_editor_ui_tests ADD_TAGS_AS_LABELS)
