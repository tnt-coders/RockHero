# Editor executable: JUCE hosts the current waveform/transport authoring surface.
juce_add_gui_app(rock_hero_editor PRODUCT_NAME "Rock Hero Editor" VERSION ${PROJECT_VERSION})

# Keep the shipped executable name stable and readable on desktop platforms.
set_target_properties(rock_hero_editor PROPERTIES OUTPUT_NAME "rock-hero-editor")

# The editor target is still intentionally flat while the app remains small.
target_sources(rock_hero_editor PRIVATE main.cpp main_window.cpp)

# Main-window implementation includes stay local to this small target for now.
target_include_directories(rock_hero_editor PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# Generate JUCE's project metadata umbrella for this app target.
rock_hero_juce_generate_juce_header(rock_hero_editor)

# Compose the editor from UI/audio libraries plus the JUCE wrappers exposed by their headers.
target_link_libraries(
    rock_hero_editor PRIVATE rock_hero_ui rock_hero_audio rock_hero_juce_gui_basics
                             rock_hero_juce_audio_utils rock_hero::build_policy)

# Register the editor as a developer-facing desktop target in the package metadata.
package_register_target(rock_hero_editor DISPLAY_NAME "Rock Hero Editor")
