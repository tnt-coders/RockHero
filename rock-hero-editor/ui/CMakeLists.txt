# Package the static SVG editor UI icons into a JUCE binary-data target.
rock_hero_juce_add_binary_data(rock_hero_editor_ui_resources SOURCES resources/play.svg
                               resources/pause.svg resources/stop.svg resources/help.svg)

add_library(rock_hero_editor_ui STATIC)
add_library(rock_hero::editor::ui ALIAS rock_hero_editor_ui)

# JUCE editor presentation owns concrete widgets and the top-level editor window shell.
# Sources are grouped by feature folder; folders are navigation only and add no targets.
target_sources(
    rock_hero_editor_ui
    PRIVATE src/audio_device/audio_device_failure_overlay.cpp
            src/audio_device/audio_device_failure_overlay.h
            src/audio_device/audio_device_settings_view.cpp
            src/audio_device/audio_device_settings_view.h
            src/audio_device/audio_device_settings_window.cpp
            src/audio_device/audio_device_settings_window.h
            src/audio_device/game_audio_recommendation_dialog.cpp
            src/audio_device/game_audio_recommendation_dialog.h
            src/busy/busy_overlay.cpp
            src/busy/busy_overlay.h
            src/input_calibration/input_calibration_window.cpp
            src/input_calibration/input_calibration_window.h
            src/main_window/editor.cpp
            src/main_window/editor_view.cpp
            src/main_window/editor_view.h
            src/main_window/main_window.cpp
            src/main_window/menu_bar_button.cpp
            src/main_window/menu_bar_button.h
            src/main_window/menu_look_and_feel.cpp
            src/main_window/menu_look_and_feel.h
            src/main_window/undo_history_overlay.cpp
            src/main_window/undo_history_overlay.h
            src/preview/preview_resources.cpp
            src/preview/preview_resources.h
            src/preview/preview_surface.cpp
            src/preview/preview_surface.h
            src/preview/preview_window.cpp
            src/preview/preview_window.h
            src/shared/audio_level_meter.cpp
            src/shared/audio_level_meter.h
            src/shared/editor_theme.cpp
            src/shared/editor_theme.h
            src/shared/themed_message_box.cpp
            src/shared/themed_message_box.h
            src/shared/text_metrics.cpp
            src/shared/text_metrics.h
            src/signal_chain/insert_slot_view.cpp
            src/signal_chain/insert_slot_view.h
            src/signal_chain/plugin_browser_window.cpp
            src/signal_chain/plugin_browser_window.h
            src/signal_chain/plugin_drag.cpp
            src/signal_chain/plugin_drag.h
            src/signal_chain/plugin_tile_view.cpp
            src/signal_chain/plugin_tile_view.h
            src/signal_chain/signal_chain_block_layout.cpp
            src/signal_chain/signal_chain_block_layout.h
            src/signal_chain/signal_chain_panel.cpp
            src/signal_chain/signal_chain_panel.h
            src/signal_chain/signal_chain_view.cpp
            src/signal_chain/signal_chain_view.h
            src/signal_chain/signal_chain_view_metrics.h
            src/tab/tab_view.cpp
            src/tab/tab_view.h
            src/timeline/arrangement_view.cpp
            src/timeline/arrangement_view.h
            src/timeline/cursor_overlay.cpp
            src/timeline/cursor_overlay.h
            src/timeline/grid_spacing_selector.cpp
            src/timeline/grid_spacing_selector.h
            src/timeline/timeline_cursor.cpp
            src/timeline/timeline_cursor.h
            src/timeline/timeline_ruler.cpp
            src/timeline/timeline_ruler.h
            src/timeline/track_viewport.cpp
            src/timeline/track_viewport.h
            src/tone/tone_automation_lanes_view.cpp
            src/tone/tone_automation_lanes_view.h
            src/tone/tone_track_view.cpp
            src/tone/tone_track_view.h
            src/transport/transport_controls.cpp
            src/transport/transport_controls.h)

# The PRIVATE src dir lets library and test translation units reference private headers with
# feature-qualified quoted includes ("timeline/track_viewport.h") across feature folders.
target_include_directories(
    rock_hero_editor_ui
    PUBLIC include
    PRIVATE include/rock_hero/editor/ui src)

# common::ui supplies the shared string-color palette the tab renderer converts to JUCE colors.
target_link_libraries(
    rock_hero_editor_ui
    PUBLIC rock_hero::editor::core rock_hero::juce_gui_basics
    PRIVATE rock_hero::build_policy rock_hero::common::audio rock_hero::common::ui
            rock_hero::editor_ui_resources rock_hero::juce_audio_devices rock_hero::juce_graphics)

target_compile_definitions(
    rock_hero_editor_ui
    PRIVATE ROCK_HERO_INSTALLED_DOCS_RELATIVE_PATH="${PROJECT_CONFIG_DOCS_INSTALL_DESTINATION}"
            ROCK_HERO_BUILD_DOCS_DIR="${PROJECT_CONFIG_DOCS_OUTPUT_DIR}")

add_subdirectory(tests)
