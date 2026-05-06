# Framework-free domain library for session and song value types plus their rules.
add_library(rock_hero_core STATIC)
add_library(rock_hero::core ALIAS rock_hero_core)

find_package(libzip REQUIRED)
find_package(nlohmann_json REQUIRED)

# Keep the target surface small while core owns song/session state and project persistence.
target_sources(rock_hero_core PRIVATE src/project.cpp src/session.cpp src/song.cpp)

# Expose namespaced public headers while preserving short includes inside the owning module.
target_include_directories(
    rock_hero_core
    PUBLIC include
    PRIVATE include/rock_hero/core)

# Apply shared first-party policy and keep project-format dependencies private to core.
target_link_libraries(rock_hero_core PRIVATE rock_hero::build_policy libzip::zip
                                             nlohmann_json::nlohmann_json)

# Keep the module's pure-domain tests adjacent to the library they validate.
add_subdirectory(tests)
