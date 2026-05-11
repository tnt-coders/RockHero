# Headless editor workflow tests exercise controller policy without constructing JUCE widgets.
find_package(Catch2 3 REQUIRED)
find_package(libzip REQUIRED)
include(Catch)

add_executable(rock_hero_editor_core_tests test_editor_controller.cpp test_project.cpp)

target_link_libraries(
    rock_hero_editor_core_tests PRIVATE rock_hero::editor::core rock_hero::build_policy libzip::zip
                                        Catch2::Catch2WithMain)

# Register the discovered Catch2 cases with CTest and forward Catch tags as CTest labels.
catch_discover_tests(rock_hero_editor_core_tests ADD_TAGS_AS_LABELS REPORTER JSON)
