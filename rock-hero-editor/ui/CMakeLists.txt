# Package the static SVG editor UI icons into a JUCE binary-data target.
rock_hero_juce_add_binary_data(rock_hero_editor_ui_resources SOURCES resources/play.svg
                               resources/pause.svg resources/stop.svg resources/help.svg)

add_library(rock_hero_editor_ui STATIC)
add_library(rock_hero::editor::ui ALIAS rock_hero_editor_ui)

# JUCE editor presentation owns concrete widgets and the top-level editor window shell.
# Sources are grouped by feature folder; folders are navigation only and add no targets.
target_sources(
    rock_hero_editor_ui
    PRIVATE src/audio_device/audio_device_settings_view.cpp
            src/audio_device/audio_device_settings_window.cpp
            src/busy/busy_overlay.cpp
            src/input_calibration/input_calibration_window.cpp
            src/shared/audio_level_meter.cpp
            src/shared/text_metrics.cpp
            src/shell/editor.cpp
            src/shell/editor_view.cpp
            src/shell/main_window.cpp
            src/shell/menu_bar_button.cpp
            src/shell/menu_look_and_feel.cpp
            src/signal_chain/plugin_browser_window.cpp
            src/signal_chain/signal_chain_block_layout.cpp
            src/signal_chain/signal_chain_panel.cpp
            src/signal_chain/signal_chain_view.cpp
            src/timeline/arrangement_view.cpp
            src/timeline/cursor_overlay.cpp
            src/timeline/grid_spacing_selector.cpp
            src/timeline/timeline_cursor.cpp
            src/timeline/timeline_ruler.cpp
            src/timeline/track_viewport.cpp
            src/transport/transport_controls.cpp)

# The PRIVATE src dir lets library and test translation units reference private headers with
# feature-qualified quoted includes ("timeline/track_viewport.h") across feature folders.
target_include_directories(
    rock_hero_editor_ui
    PUBLIC include
    PRIVATE include/rock_hero/editor/ui src)

target_link_libraries(
    rock_hero_editor_ui
    PUBLIC rock_hero::editor::core rock_hero::common::audio rock_hero::juce_gui_basics
    PRIVATE rock_hero::juce_audio_devices rock_hero::build_policy rock_hero::editor_ui_resources)

target_compile_definitions(
    rock_hero_editor_ui
    PRIVATE ROCK_HERO_INSTALLED_DOCS_RELATIVE_PATH="${PROJECT_CONFIG_DOCS_INSTALL_DESTINATION}"
            ROCK_HERO_BUILD_DOCS_DIR="${PROJECT_CONFIG_DOCS_OUTPUT_DIR}")

add_subdirectory(tests)
