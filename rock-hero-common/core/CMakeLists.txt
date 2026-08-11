add_library(rock_hero_common_core STATIC)
add_library(rock_hero::common::core ALIAS rock_hero_common_core)

# Shared headless domain library for song/session state and native song package persistence.
target_sources(
    rock_hero_common_core
    PRIVATE src/chart/chart.cpp
            src/chart/chart_document.cpp
            src/chart/chart_legato.cpp
            src/chart/chart_rules.cpp
            src/chart/chart_tokens.cpp
            src/chart/grid_arithmetic.cpp
            src/highway/highway_camera.cpp
            src/highway/highway_hit_glow.cpp
            src/highway/highway_projection.cpp
            src/highway/highway_tail.cpp
            src/highway/highway_window.cpp
            src/package/archive_error.cpp
            src/package/package_id.cpp
            src/package/rock_song_package_format.cpp
            src/package/rock_song_package_format.h
            src/package/rock_song_package_read.cpp
            src/package/rock_song_package_write.cpp
            src/package/song_package_error.cpp
            src/package/workspace_paths.cpp
            src/session/session.cpp
            src/shared/json.cpp
            src/shared/juce_path.cpp
            src/shared/logger.cpp
            src/shared/logger_error.cpp
            src/song/arrangement.cpp
            src/song/song.cpp
            src/tab/tab_projection.cpp
            src/timeline/tempo_map.cpp
            src/tone/tone_automation.cpp
            src/tone/tone_track_rules.cpp
            src/tone/tone_track_edits.cpp
            src/tone/tone_track_normalize.cpp
            src/tone/tone_schedule.cpp
            src/package/package_description.cpp
            src/package/song_document_json.cpp
            src/package/song_document_json.h)

target_include_directories(
    rock_hero_common_core
    PUBLIC include
    PRIVATE include/rock_hero/common/core)

# Quill backs the project logging facade; its macros expand at call sites, so it is a public
# dependency of every consumer that logs through <rock_hero/common/core/logger.h>.
find_package(quill REQUIRED)

# Hide Quill's broad LOG_* macro aliases from first-party consumers; project code uses RH_LOG_*.
target_compile_definitions(rock_hero_common_core PUBLIC QUILL_DISABLE_NON_PREFIXED_MACROS=1)

# JSON package helpers expose JUCE core utility types through a project-owned public header.
#
# juce_data_structures is INTERFACE, not PUBLIC, on purpose: settings_file_options.h states the one
# settings-file location policy the editor settings, the game settings, and the audio-config store
# all open their properties files with, and that policy is expressed as
# juce::PropertiesFile::Options. Consumers of that header therefore need the module, but common core
# itself compiles against nothing from it — so an INTERFACE link keeps the module's own sources
# inside their narrow juce_core grant, and a common-core source that reached for PropertiesFile
# would fail to compile rather than quietly widening the grant.
target_link_libraries(
    rock_hero_common_core
    PUBLIC rock_hero::juce_core quill::quill
    INTERFACE rock_hero::juce_data_structures
    PRIVATE rock_hero::build_policy)

add_subdirectory(tests)
