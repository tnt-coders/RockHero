# Catch2 covers headless game-core behavior, starting with the resource resolver that every game
# plan loads packaged assets through.
find_package(Catch2 3 REQUIRED)
include(Catch)

add_executable(
    rock_hero_game_core_tests
    test_diagnostics.cpp
    test_frame_clock.cpp
    test_game_audio_config.cpp
    test_native_audio_setup.cpp
    test_game_resources.cpp
    test_game_settings.cpp
    test_gameplay_session.cpp
    test_library_index_store.cpp
    test_library_scan_engine.cpp
    test_library_scan_plan.cpp
    test_library_scan_roots.cpp
    test_menu_bindings.cpp
    test_song_select_menu.cpp)

# Shared test fakes (the null settings port) live under tests/include, mirroring editor/core.
target_include_directories(rock_hero_game_core_tests PRIVATE include)

# Tests consume the resolver through its public include form; the library under test carries the
# public include path. juce_events supplies the ScopedJuceInitialiser_GUI guard the
# PropertiesFile-backed settings tests need. common::audio_testing carries the shared live-input
# and audio-config fakes (with their juce_audio_devices dependency) the gameplay-session tests
# drive the LiveInputMonitor through.
target_link_libraries(
    rock_hero_game_core_tests
    PRIVATE rock_hero::game::core rock_hero::build_policy rock_hero::juce_events
            rock_hero::common::audio_testing Catch2::Catch2WithMain)

# Register the discovered Catch2 cases with CTest and forward Catch tags as CTest labels.
catch_discover_tests(rock_hero_game_core_tests ADD_TAGS_AS_LABELS)
