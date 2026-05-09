# Editor application shell library: composes app-local settings, audio, and UI window ownership.
add_library(rock_hero_editor STATIC)
add_library(rock_hero::editor ALIAS rock_hero_editor)

# Keep app-shell behavior testable by keeping it outside the thin JUCE executable target.
target_sources(rock_hero_editor PRIVATE editor_settings.cpp main_window.cpp)

# Expose local app-shell headers to the launcher and adjacent tests.
target_include_directories(rock_hero_editor PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# MainWindow's public header derives from JUCE, while audio/UI concrete composition stays private.
target_link_libraries(
    rock_hero_editor
    PUBLIC rock_hero::juce_gui_basics
    PRIVATE rock_hero::ui rock_hero::audio rock_hero::juce_audio_utils rock_hero::build_policy)

# Keep the editor shell's tests adjacent to the app-specific library they validate.
add_subdirectory(tests)

# Editor executable: JUCE hosts the current waveform/transport authoring surface.
juce_add_gui_app(rock_hero_editor_exe PRODUCT_NAME "Rock Hero Editor" VERSION ${PROJECT_VERSION})

# Keep the shipped executable name stable and readable on desktop platforms.
set_target_properties(rock_hero_editor_exe PROPERTIES OUTPUT_NAME "rock-hero-editor")

# The executable owns only process startup; the app behavior lives in rock_hero_editor.
target_sources(rock_hero_editor_exe PRIVATE main.cpp)

# Generate JUCE's project metadata umbrella for this app target.
rock_hero_juce_generate_juce_header(rock_hero_editor_exe)

# Link the launcher to the testable editor shell plus shared first-party build policy.
target_link_libraries(rock_hero_editor_exe PRIVATE rock_hero::editor rock_hero::build_policy)

# Register the editor as a developer-facing desktop target in the package metadata.
package_register_target(rock_hero_editor_exe DISPLAY_NAME "Rock Hero Editor")
