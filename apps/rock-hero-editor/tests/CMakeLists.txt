# Catch2 powers the editor app-shell tests for behavior moved out of the executable target.
find_package(Catch2 3 REQUIRED)
include(Catch)

# Fast app-shell tests cover editor-specific helpers without launching the JUCE application.
add_executable(rock_hero_editor_tests test_editor_settings.cpp)

# Link the editor shell library under test and pass a build-local directory for settings files.
target_link_libraries(rock_hero_editor_tests PRIVATE rock_hero::editor rock_hero::build_policy
                                                     Catch2::Catch2WithMain)
target_compile_definitions(rock_hero_editor_tests
                           PRIVATE TEST_SETTINGS_DIR="${CMAKE_CURRENT_BINARY_DIR}")

# Register the discovered Catch2 cases with CTest and forward Catch tags as CTest labels.
catch_discover_tests(rock_hero_editor_tests ADD_TAGS_AS_LABELS)
