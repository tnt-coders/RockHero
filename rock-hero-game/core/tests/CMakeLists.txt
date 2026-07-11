# Catch2 covers headless game-core behavior, starting with the resource resolver that every game
# plan loads packaged assets through.
find_package(Catch2 3 REQUIRED)
include(Catch)

add_executable(rock_hero_game_core_tests test_game_resources.cpp)

# Tests consume the resolver through its public include form; the library under test carries the
# public include path.
target_link_libraries(rock_hero_game_core_tests
                      PRIVATE rock_hero::game::core rock_hero::build_policy Catch2::Catch2WithMain)

# Register the discovered Catch2 cases with CTest and forward Catch tags as CTest labels.
catch_discover_tests(rock_hero_game_core_tests ADD_TAGS_AS_LABELS)
