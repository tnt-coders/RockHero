add_library(rock_hero_editor_core STATIC)
add_library(rock_hero::editor::core ALIAS rock_hero_editor_core)

# Headless editor workflow and project persistence stay testable without owning JUCE widgets.
# Sources are grouped by feature folder; root files are the cross-feature hub contract.
target_sources(
    rock_hero_editor_core
    PRIVATE src/audio_device/audio_device_settings_controller.cpp
            src/audio_device/audio_device_status_text.cpp
            src/busy/busy_operation_state.cpp
            src/busy/busy_operation_state.h
            src/busy/busy_operation_workflow.cpp
            src/busy/busy_operation_workflow.h
            src/busy/busy_view_state.cpp
            src/deferred_project_action_state.cpp
            src/deferred_project_action_state.h
            src/editor_action.cpp
            src/editor_action.h
            src/editor_action_availability.cpp
            src/editor_action_availability.h
            src/editor_controller.cpp
            src/editor_controller_impl.h
            src/editor_undo_history.cpp
            src/editor_undo_history.h
            src/input_calibration/input_calibration_controller.cpp
            src/input_calibration/input_calibration_workflow.cpp
            src/input_calibration/input_calibration_workflow.h
            src/project/project.cpp
            src/project/project_error.cpp
            src/project/project_io.cpp
            src/project/rock_song_importer.cpp
            src/project/song_import_error.cpp
            src/settings/editor_settings.cpp
            src/settings/editor_settings_error.cpp
            src/signal_chain/plugin_catalog_workflow.cpp
            src/signal_chain/plugin_catalog_workflow.h
            src/signal_chain/plugin_display_type.cpp
            src/signal_chain/signal_chain_block_placement.cpp
            src/signal_chain/signal_chain_edits.cpp
            src/signal_chain/signal_chain_edits.h
            src/signal_chain/signal_chain_workflow.cpp
            src/signal_chain/signal_chain_workflow.h
            src/tasks/juce_editor_task_runner.cpp
            src/tasks/juce_message_thread_scheduler.cpp
            src/timeline/tempo_grid_geometry.cpp
            src/timeline/timeline_geometry.cpp
            src/timeline/transport_readout_text.cpp)

# The PRIVATE src dir lets library and test translation units reference private headers with
# feature-qualified quoted includes ("busy/busy_operation_state.h") across feature folders.
target_include_directories(
    rock_hero_editor_core
    PUBLIC include
    PRIVATE include/rock_hero/editor/core src)

# EditorSettings stores juce::PropertiesFile in its public header, so consumers need this wrapper.
target_link_libraries(
    rock_hero_editor_core
    PUBLIC rock_hero::common::core rock_hero::common::audio rock_hero::juce_data_structures
    PRIVATE rock_hero::build_policy rock_hero::juce_core rock_hero::juce_events)

add_subdirectory(tests)
