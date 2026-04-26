# Catch2 powers the headless UI contract tests for controller/view interfaces.
find_package(Catch2 3 REQUIRED)
include(Catch)

# Headless contract and controller-behavior tests for the framework-free UI layer.
add_executable(rock_hero_ui_tests test_editor_controller.cpp test_track_view.cpp)

# Link the concrete UI library so tests inherit its public headers and transitive dependencies.
target_link_libraries(rock_hero_ui_tests PRIVATE rock_hero_ui Catch2::Catch2WithMain)

# Register the discovered Catch2 cases with CTest and forward Catch tags as CTest labels.
catch_discover_tests(rock_hero_ui_tests ADD_TAGS_AS_LABELS REPORTER JSON)
