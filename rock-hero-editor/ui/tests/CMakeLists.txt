find_package(Catch2 3 REQUIRED)
include(Catch)

add_executable(
    rock_hero_editor_ui_tests test_arrangement_view.cpp test_editor.cpp test_editor_settings.cpp
                              test_editor_view.cpp test_transport_controls.cpp)

target_link_libraries(rock_hero_editor_ui_tests
                      PRIVATE rock_hero::editor::ui rock_hero::build_policy Catch2::Catch2WithMain)
target_compile_definitions(rock_hero_editor_ui_tests
                           PRIVATE TEST_SETTINGS_DIR="${CMAKE_CURRENT_BINARY_DIR}")

catch_discover_tests(rock_hero_editor_ui_tests ADD_TAGS_AS_LABELS)
