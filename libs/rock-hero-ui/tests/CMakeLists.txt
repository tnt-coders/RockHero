# Catch2 powers the headless UI contract tests for controller/view interfaces.
find_package(Catch2 3 REQUIRED)
include(Catch)

# Keep JUCE-free contract tests separate from the concrete JUCE component library target.
add_executable(rock_hero_ui_tests test_editor_controller.cpp)

# Link only the framework-free dependencies needed by the controller contract headers.
target_link_libraries(rock_hero_ui_tests PRIVATE rock_hero_core Catch2::Catch2WithMain)

# Expose the UI module's public headers without pulling the JUCE-backed rock_hero_ui target in.
target_include_directories(rock_hero_ui_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../include")

# Register the discovered Catch2 cases with CTest and forward Catch tags as CTest labels.
catch_discover_tests(rock_hero_ui_tests ADD_TAGS_AS_LABELS)
