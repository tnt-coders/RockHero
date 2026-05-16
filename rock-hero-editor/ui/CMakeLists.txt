# Package the static SVG control icons into a JUCE binary-data target for the editor UI.
rock_hero_juce_add_binary_data(rock_hero_editor_ui_resources SOURCES resources/play_arrow.svg
                               resources/pause.svg resources/stop.svg)

add_library(rock_hero_editor_ui STATIC)
add_library(rock_hero::editor::ui ALIAS rock_hero_editor_ui)

# JUCE editor presentation owns concrete widgets and the top-level editor window shell.
target_sources(
    rock_hero_editor_ui
    PRIVATE src/arrangement_view.cpp
            src/audio_device_settings_component.cpp
            src/audio_device_settings_dialog.cpp
            src/editor.cpp
            src/editor_view.cpp
            src/instrument_panel.cpp
            src/main_window.cpp
            src/transport_controls.cpp)

target_include_directories(
    rock_hero_editor_ui
    PUBLIC include
    PRIVATE include/rock_hero/editor/ui)

target_link_libraries(
    rock_hero_editor_ui
    PUBLIC rock_hero::editor::core rock_hero::common::audio rock_hero::juce_gui_basics
    PRIVATE rock_hero::juce_audio_devices rock_hero::build_policy rock_hero::editor_ui_resources)

add_subdirectory(tests)
