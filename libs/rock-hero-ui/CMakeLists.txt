# Package the static SVG control icons into a small JUCE binary-data target for the UI module.
rock_hero_juce_add_binary_data(rock_hero_ui_resources SOURCES resources/play_arrow.svg
                               resources/pause.svg resources/stop.svg)

# JUCE-facing editor library for views, controllers, and headless edit workflow coordination.
add_library(rock_hero_ui STATIC)
add_library(rock_hero::ui ALIAS rock_hero_ui)

# Keep the target surface small while the UI module remains focused on editor workflow and views.
target_sources(
    rock_hero_ui PRIVATE src/track_view.cpp src/transport_controls.cpp src/edit_coordinator.cpp
                         src/editor_controller.cpp src/editor_view.cpp src/editor.cpp)

# Expose namespaced public headers while preserving short includes inside the owning module.
target_include_directories(
    rock_hero_ui
    PUBLIC include
    PRIVATE include/rock_hero/ui)

# Export the UI library plus the JUCE wrapper needed by its public component headers.
target_link_libraries(
    rock_hero_ui
    PUBLIC rock_hero::audio rock_hero::juce_gui_basics
    PRIVATE rock_hero::build_policy rock_hero::ui_resources)

# Keep the module's headless controller-contract tests adjacent to the UI library.
add_subdirectory(tests)
