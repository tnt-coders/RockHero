# Package the static SVG control icons into a small JUCE binary-data target for the UI module.
rock_hero_juce_add_binary_data(rock_hero_ui_resources SOURCES resources/play_arrow.svg
                               resources/pause.svg resources/stop.svg)

# JUCE-facing presentation library for waveform display, track views, and transport controls.
add_library(rock_hero_ui STATIC)

# Keep the target surface small while the UI module remains focused on playback presentation.
target_sources(rock_hero_ui PRIVATE src/waveform_display.cpp src/track_view.cpp
                                    src/transport_controls.cpp src/editor_controller.cpp)

# Expose namespaced public headers while preserving short includes inside the owning module.
target_include_directories(
    rock_hero_ui
    PUBLIC include
    PRIVATE include/rock_hero/ui)

# Export the UI library plus the JUCE wrapper needed by its public component headers.
target_link_libraries(
    rock_hero_ui
    PUBLIC rock_hero_audio rock_hero_juce_gui_basics
    PRIVATE rock_hero_ui_resources)

# Keep the module's headless controller-contract tests adjacent to the UI library.
add_subdirectory(tests)
