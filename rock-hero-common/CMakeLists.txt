add_subdirectory(core)
add_subdirectory(audio)
add_subdirectory(ui)

# Scope umbrella for app composition; libraries should keep linking the narrow submodules.
add_library(rock_hero_common INTERFACE)
add_library(rock_hero::common ALIAS rock_hero_common)

# Export every common submodule through one app-facing aggregate without adding implementation.
target_link_libraries(rock_hero_common INTERFACE rock_hero::common::core rock_hero::common::audio
                                                 rock_hero::common::ui)
