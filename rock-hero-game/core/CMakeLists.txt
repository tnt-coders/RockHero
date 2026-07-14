add_library(rock_hero_game_core STATIC)
add_library(rock_hero::game::core ALIAS rock_hero_game_core)

# Resources feature: the typed resolver every game plan loads packaged assets through
# (plan 20 Phase 2's resource-pack convention). Frame-clock feature: pure per-frame playback
# clock consumption and pacing stats for the game loop (plan 20 Phase 3). Diagnostics feature:
# headless dev-diagnostics state, intents, and the chart-source change detector (plan 20 Phase 4).
# Session feature: the headless GameplaySession orchestration that loads a package, preloads the
# tone rig, and drives playback through the shared audio ports (plan 21 Phase 2). Settings
# feature: the game's one per-user persistence seam and the implicit v1 profile (plan 27
# Phase 1), mirroring the editor's PropertiesFile-backed adapter. Library feature: the cached
# song-library index (versioned JSON, atomic writes, rebuild-on-doubt loads), the pure scan
# planner that diffs it against on-disk package facts (plan 26 Phase 2), and the step-driven scan
# engine that executes a plan through injected lister/describer/album-art ports, signalling commit
# checkpoints instead of persisting itself (plan 26 Phase 3), and the pure scan-root resolver that
# composes the default per-user Songs folder with the user's custom roots from settings
# (plan 26 Phase 4). Input feature: the headless menu-action vocabulary and the bindable resolver
# that maps device-agnostic input triggers to actions with overwrite-and-clear rebinding
# (plan 26 Phase 5); concrete device defaults and adapters live at the composition boundary.
target_sources(
    rock_hero_game_core
    PRIVATE src/diagnostics/diagnostics.cpp
            src/frame_clock/frame_clock.cpp
            src/input/menu_bindings.cpp
            src/library/album_art_error.cpp
            src/library/library_index_store.cpp
            src/library/library_index_store_error.cpp
            src/library/library_entry_projection.cpp
            src/library/library_scan_engine.cpp
            src/library/library_scan_plan.cpp
            src/library/library_scan_roots.cpp
            src/library/null_album_art_generator.cpp
            src/resources/game_resources.cpp
            src/session/gameplay_session.cpp
            src/session/gameplay_session_error.cpp
            src/settings/game_settings.cpp
            src/settings/game_settings_error.cpp)

target_include_directories(
    rock_hero_game_core
    PUBLIC include
    PRIVATE include/rock_hero/game/core)

# PUBLIC because the frame-clock header exposes the shared clock snapshot, extrapolation policy,
# and timeline types (the headless clock surface of common/audio — same precedent as editor/core).
# juce_data_structures is PUBLIC because the game settings adapter header exposes
# juce::PropertiesFile (same precedent as editor/core's settings adapter).
target_link_libraries(
    rock_hero_game_core
    PUBLIC rock_hero::common::core rock_hero::common::audio rock_hero::juce_data_structures
    PRIVATE rock_hero::build_policy rock_hero::juce_core)

add_subdirectory(tests)
