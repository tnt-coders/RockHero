add_library(rock_hero_common_core STATIC)
add_library(rock_hero::common::core ALIAS rock_hero_common_core)

# Shared headless domain library for song/session state and native song package persistence.
target_sources(
    rock_hero_common_core
    PRIVATE src/chart/chart.cpp
            src/chart/chart_document.cpp
            src/chart/chart_fret_hand.cpp
            src/chart/chart_legato.cpp
            src/chart/chart_presentation.cpp
            src/chart/chart_projection.cpp
            src/chart/chart_rules.cpp
            src/chart/chart_shape_holds.cpp
            src/chart/chart_shapes.cpp
            src/chart/chart_tokens.cpp
            src/chart/grid_arithmetic.cpp
            src/chart/head_margins.cpp
            # Private header: the one span-coverage walk the presentation rules and the held-stop
            # default all measure against, shared so it cannot be spelled twice.
            src/chart/span_cover.h
            src/highway/highway_camera.cpp
            src/highway/highway_hit_glow.cpp
            src/highway/highway_light.cpp
            src/highway/highway_projection.cpp
            src/highway/highway_settings_error.cpp
            src/highway/highway_settings_store.cpp
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
            src/shared/decimal_text.cpp
            src/shared/json.cpp
            src/shared/juce_path.cpp
            src/shared/logger.cpp
            src/shared/logger_error.cpp
            src/shared/shared_settings_file.cpp
            src/shared/user_data_folders.cpp
            src/song/arrangement.cpp
            src/song/song.cpp
            src/song/song_section_rules.cpp
            src/song/song_timeline.cpp
            src/timeline/tempo_map.cpp
            src/timeline/tempo_map_rules.cpp
            src/timeline/tick_lattice.cpp
            src/timeline/time_signature_text.cpp
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
# juce_data_structures is PUBLIC for one purpose, the per-user settings files: the storage policy
# every settings file opens with, the file both products share and the stores over it, and the
# per-user folder JUCE keeps settings in. All of it runs headless, the test the grant turns on.
target_link_libraries(
    rock_hero_common_core
    PUBLIC rock_hero::juce_core rock_hero::juce_data_structures quill::quill
    PRIVATE rock_hero::build_policy)

add_subdirectory(tests)
