add_library(rock_hero_editor_audio STATIC)
add_library(rock_hero::editor::audio ALIAS rock_hero_editor_audio)

# Placeholder editor-audio module reserved for editor-only audio policy outside common/audio.
target_sources(rock_hero_editor_audio PRIVATE src/placeholder.cpp)

target_include_directories(
    rock_hero_editor_audio
    PUBLIC include
    PRIVATE include/rock_hero/editor/audio)

# Editor audio may consume common audio ports while avoiding editor UI/app dependencies.
target_link_libraries(
    rock_hero_editor_audio
    PUBLIC rock_hero::common::core rock_hero::common::audio
    PRIVATE rock_hero::build_policy)
