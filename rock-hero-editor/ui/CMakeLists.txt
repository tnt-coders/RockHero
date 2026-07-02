# Package the static SVG editor UI icons into a JUCE binary-data target.
rock_hero_juce_add_binary_data(rock_hero_editor_ui_resources SOURCES resources/play_arrow.svg
                               resources/pause.svg resources/stop.svg resources/help.svg)

add_library(rock_hero_editor_ui STATIC)
add_library(rock_hero::editor::ui ALIAS rock_hero_editor_ui)

# JUCE editor presentation owns concrete widgets and the top-level editor window shell.
target_sources(
    rock_hero_editor_ui
    PRIVATE src/arrangement_view.cpp
            src/audio_level_meter.cpp
            src/audio_device_settings_view.cpp
            src/audio_device_settings_window.cpp
            src/busy_overlay.cpp
            src/editor.cpp
            src/editor_view.cpp
            src/grid_spacing_selector.cpp
            src/input_calibration_window.cpp
            src/menu_bar_button.cpp
            src/plugin_browser_window.cpp
            src/signal_chain_block_layout.cpp
            src/signal_chain_panel.cpp
            src/signal_chain_view.cpp
            src/main_window.cpp
            src/timeline_cursor.cpp
            src/timeline_ruler.cpp
            src/transport_controls.cpp)

target_include_directories(
    rock_hero_editor_ui
    PUBLIC include
    PRIVATE include/rock_hero/editor/ui)

target_link_libraries(
    rock_hero_editor_ui
    PUBLIC rock_hero::editor::core rock_hero::common::audio rock_hero::juce_gui_basics
    PRIVATE rock_hero::juce_audio_devices rock_hero::build_policy rock_hero::editor_ui_resources)

target_compile_definitions(
    rock_hero_editor_ui
    PRIVATE ROCK_HERO_INSTALLED_DOCS_RELATIVE_PATH="${PROJECT_CONFIG_DOCS_INSTALL_DESTINATION}"
            ROCK_HERO_BUILD_DOCS_DIR="${PROJECT_CONFIG_DOCS_OUTPUT_DIR}")

add_subdirectory(tests)
