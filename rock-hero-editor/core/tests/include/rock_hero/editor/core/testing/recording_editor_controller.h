/*!
\file recording_editor_controller.h
\brief Recording editor controller implementation for view and controller contract tests.
*/

#pragma once

#include <cstddef>
#include <expected>
#include <filesystem>
#include <functional>
#include <optional>
#include <rock_hero/common/audio/input/live_input_error.h>
#include <rock_hero/common/core/fraction.h>
#include <rock_hero/common/core/timeline.h>
#include <rock_hero/editor/core/i_editor_controller.h>
#include <string>
#include <utility>
#include <vector>

namespace rock_hero::editor::core::testing
{

/*!
\brief IEditorController implementation that records every received editor intent.

Use this when tests need to verify that a view emits controller intents without constructing the
real EditorController workflow. The public counters and last-value fields are intentionally exposed
so tests can assert only the behavior they care about.
*/
class RecordingEditorController final : public IEditorController
{
public:
    /*!
    \brief Captures the file selected by the Open command.
    \param file Project file selected by the view.
    */
    void onOpenRequested(std::filesystem::path file) override
    {
        last_open_file = std::move(file);
        open_request_count += 1;
    }

    /*!
    \brief Captures the file selected by the Import command.
    \param file Song source selected by the view.
    */
    void onImportRequested(std::filesystem::path file) override
    {
        last_import_file = std::move(file);
        import_request_count += 1;
    }

    /*! \brief Counts Save command dispatches. */
    void onSaveRequested() override
    {
        save_request_count += 1;
    }

    /*!
    \brief Captures the destination selected by the Save As command.
    \param file Project destination selected by the view.
    */
    void onSaveAsRequested(std::filesystem::path file) override
    {
        last_save_as_file = std::move(file);
        save_as_request_count += 1;
    }

    /*!
    \brief Captures the destination selected by the Publish command.
    \param file Native song package destination selected by the view.
    */
    void onPublishRequested(std::filesystem::path file) override
    {
        last_publish_file = std::move(file);
        publish_request_count += 1;
    }

    /*! \brief Counts Save As cancellation notifications. */
    void onSaveAsCancelled() override
    {
        save_as_cancel_count += 1;
    }

    /*! \brief Counts busy-operation cancellation notifications. */
    void onBusyCancelRequested() override
    {
        busy_cancel_request_count += 1;
    }

    /*! \brief Counts Undo command dispatches. */
    void onUndoRequested() override
    {
        undo_request_count += 1;
    }

    /*! \brief Counts Redo command dispatches. */
    void onRedoRequested() override
    {
        redo_request_count += 1;
    }

    /*! \brief Counts Close command dispatches. */
    void onCloseRequested() override
    {
        close_request_count += 1;
    }

    /*! \brief Counts Exit command dispatches. */
    void onExitRequested() override
    {
        exit_request_count += 1;
    }

    /*!
    \brief Captures prompt decisions selected through the unsaved-changes dialog.
    \param decision User-selected unsaved-changes decision.
    */
    void onUnsavedChangesDecision(UnsavedChangesDecision decision) override
    {
        last_unsaved_changes_decision = decision;
        unsaved_changes_decision_count += 1;
    }

    /*!
    \brief Captures interrupted-restore prompt decisions emitted by a view.
    \param decision User-selected interrupted-restore decision.
    */
    void onRestoreInterruptedDecision(RestoreInterruptedDecision decision) override
    {
        last_restore_interrupted_decision = decision;
        restore_interrupted_decision_count += 1;
    }

    /*! \brief Counts play/pause intents emitted by keyboard or transport controls. */
    void onPlayPausePressed() override
    {
        play_pause_press_count += 1;
    }

    /*! \brief Counts stop intents emitted by transport controls. */
    void onStopPressed() override
    {
        stop_press_count += 1;
    }

    /*!
    \brief Captures the timeline seek position emitted by timeline click handling.
    \param position Requested seek position on the song timeline.
    */
    void onTimelineSeekRequested(common::core::TimePosition position) override
    {
        last_seek_position = position;
        timeline_seek_count += 1;
    }

    /*!
    \brief Captures grid note-value change intents emitted by the grid selector.
    \param note_value Grid step as a fraction of a whole note.
    */
    void onGridNoteValueChangeRequested(common::core::Fraction note_value) override
    {
        last_grid_note_value = note_value;
        grid_note_value_change_count += 1;
    }

    /*!
    \brief Records the view-reported timeline zoom.
    \param pixels_per_second Reported horizontal timeline scale.
    */
    void onTimelineZoomChanged(double pixels_per_second) override
    {
        last_timeline_zoom_pixels_per_second = pixels_per_second;
        timeline_zoom_change_count += 1;
    }

    /*! \brief Counts plugin-browser open intents emitted by the signal-chain panel. */
    void onPluginBrowserRequested() override
    {
        plugin_browser_request_count += 1;
    }

    /*!
    \brief Captures insertion-slot selections emitted by signal-chain gap controls.
    \param chain_index User-visible insertion slot.
    \param block_index Fixed visual block selected for the insertion.
    */
    void onPluginInsertSlotSelected(std::size_t chain_index, std::size_t block_index) override
    {
        last_plugin_insert_slot = chain_index;
        last_plugin_insert_block = block_index;
        plugin_insert_slot_selection_count += 1;
    }

    /*! \brief Counts plugin-browser close intents emitted by the browser window. */
    void onPluginBrowserClosed() override
    {
        plugin_browser_close_count += 1;
    }

    /*! \brief Counts plugin-catalog scan intents emitted by the browser window. */
    void onPluginCatalogScanRequested() override
    {
        plugin_catalog_scan_request_count += 1;
    }

    /*!
    \brief Captures selected plugin IDs requested for insertion from the browser window.
    \param plugin_id Opaque plugin candidate ID selected by the view.
    */
    void onSelectedPluginInsertRequested(std::string plugin_id) override
    {
        last_selected_plugin_id = std::move(plugin_id);
        selected_plugin_insert_request_count += 1;
    }

    /*!
    \brief Captures plugin instances selected through the signal-chain panel.
    \param instance_id Opaque plugin instance ID selected by the view.
    */
    void onRemovePluginRequested(std::string instance_id) override
    {
        last_removed_plugin_instance_id = std::move(instance_id);
        remove_plugin_request_count += 1;
    }

    /*!
    \brief Captures plugin moves selected through the signal-chain panel.
    \param instance_id Opaque plugin instance ID selected by the view.
    \param destination_index Final user-visible chain index selected by the view.
    \param placement Fixed visual block assignments after the move.
    */
    void onMovePluginRequested(
        std::string instance_id, std::size_t destination_index,
        std::vector<PluginBlockAssignment> placement) override
    {
        last_moved_plugin_instance_id = std::move(instance_id);
        last_move_plugin_destination_index = destination_index;
        last_move_plugin_placement = std::move(placement);
        move_plugin_request_count += 1;
    }

    /*!
    \brief Captures the authored visual block placement reported for persistence.
    \param placement Fixed visual block assignments reported by the view.
    */
    void onSignalChainPlacementChanged(std::vector<PluginBlockAssignment> placement) override
    {
        last_signal_chain_placement = std::move(placement);
        signal_chain_placement_change_count += 1;
    }

    /*!
    \brief Captures manual display type overrides selected through the signal-chain panel.
    \param instance_id Opaque plugin instance ID selected by the view.
    \param display_type Manual display type, or empty to clear an override.
    */
    void onPluginDisplayTypeOverrideChanged(
        std::string instance_id, std::optional<PluginDisplayType> display_type) override
    {
        last_display_type_override_instance_id = std::move(instance_id);
        last_display_type_override = display_type;
        plugin_display_type_override_change_count += 1;
    }

    /*!
    \brief Captures plugin instances selected for editor-window opening.
    \param instance_id Opaque plugin instance ID selected by the view.
    */
    void onOpenPluginRequested(std::string instance_id) override
    {
        last_opened_plugin_instance_id = std::move(instance_id);
        open_plugin_request_count += 1;
    }

    /*! \brief Counts manual input calibration requests emitted by the signal-chain panel. */
    void onInputCalibrationRequested() override
    {
        input_calibration_request_count += 1;
    }

    /*!
    \brief Records calibration measurement setup through the controller contract.
    \return Always empty success.
    */
    [[nodiscard]] std::expected<void, common::audio::LiveInputError>
    onInputCalibrationMeasurementStarted() override
    {
        input_calibration_measurement_start_count += 1;
        return {};
    }

    /*! \brief Records calibration measurement cancellation through the controller contract. */
    void onInputCalibrationMeasurementCancelled() override
    {
        input_calibration_measurement_cancel_count += 1;
    }

    /*!
    \brief Records automatic calibration completion through the controller contract.
    \param gain_db Calibration gain selected by automatic measurement.
    \return Always empty success.
    */
    [[nodiscard]] std::expected<void, common::audio::LiveInputError> onInputCalibrationSucceeded(
        double gain_db) override
    {
        last_input_calibration_gain_db = gain_db;
        input_calibration_success_count += 1;
        return {};
    }

    /*!
    \brief Records manual calibration completion through the controller contract.
    \param gain_db Calibration gain selected by the user.
    \return Always empty success.
    */
    [[nodiscard]] std::expected<void, common::audio::LiveInputError> onInputCalibrationManuallySet(
        double gain_db) override
    {
        last_input_calibration_gain_db = gain_db;
        input_calibration_manual_set_count += 1;
        return {};
    }

    /*! \brief Counts dismissed input calibration prompts. */
    void onInputCalibrationDismissed() override
    {
        input_calibration_dismiss_count += 1;
    }

    /*!
    \brief Records output gain preview intents emitted by the signal-chain panel.
    \param gain_db Output gain preview selected by the view.
    */
    void onOutputGainPreviewChanged(double gain_db) override
    {
        last_output_gain_preview_db = gain_db;
        output_gain_preview_change_count += 1;
    }

    /*!
    \brief Records output gain commit intents emitted by the signal-chain panel.
    \param gain_db Output gain selected by the view.
    */
    void onOutputGainChanged(double gain_db) override
    {
        last_output_gain_db = gain_db;
        output_gain_change_count += 1;
    }

    /*!
    \brief Records audio-device change scheduling and stores the supplied completion callback.
    \param change_audio_device Callback that performs the audio-device mutation.
    \param after_busy_cleared Callback to invoke after the busy overlay clears.
    */
    void onAudioDeviceChangeRequested(
        std::function<void()> change_audio_device,
        std::function<void()> after_busy_cleared) override
    {
        last_audio_device_change = std::move(change_audio_device);
        last_audio_device_after_busy_cleared = std::move(after_busy_cleared);
        audio_device_change_request_count += 1;
    }

    /*!
    \brief Counts accepted audio-device settings open requests.
    \return Always true to accept the open request.
    */
    bool onAudioDeviceSettingsOpenRequested() override
    {
        audio_device_settings_open_count += 1;
        return true;
    }

    /*! \brief Counts audio-device settings close notifications. */
    void onAudioDeviceSettingsClosed() override
    {
        audio_device_settings_close_count += 1;
    }

    /*! \brief Last file passed to onOpenRequested(). */
    std::optional<std::filesystem::path> last_open_file{};

    /*! \brief Last file passed to onImportRequested(). */
    std::optional<std::filesystem::path> last_import_file{};

    /*! \brief Last destination passed to onSaveAsRequested(). */
    std::optional<std::filesystem::path> last_save_as_file{};

    /*! \brief Last destination passed to onPublishRequested(). */
    std::optional<std::filesystem::path> last_publish_file{};

    /*! \brief Last timeline seek position emitted by the view. */
    std::optional<common::core::TimePosition> last_seek_position{};

    /*! \brief Last grid note value emitted by the view, as a fraction of a whole note. */
    std::optional<common::core::Fraction> last_grid_note_value{};

    /*! \brief Last timeline zoom reported through onTimelineZoomChanged(). */
    std::optional<double> last_timeline_zoom_pixels_per_second{};

    /*! \brief Last plugin ID selected through the plugin browser. */
    std::optional<std::string> last_selected_plugin_id{};

    /*! \brief Last insertion slot selected through the signal-chain panel. */
    std::optional<std::size_t> last_plugin_insert_slot{};

    /*! \brief Last visual block selected for a signal-chain insertion. */
    std::optional<std::size_t> last_plugin_insert_block{};

    /*! \brief Last plugin instance ID selected through the signal-chain panel. */
    std::optional<std::string> last_removed_plugin_instance_id{};

    /*! \brief Last plugin instance ID selected for a move request. */
    std::optional<std::string> last_moved_plugin_instance_id{};

    /*! \brief Last plugin destination index selected for a move request. */
    std::optional<std::size_t> last_move_plugin_destination_index{};

    /*! \brief Last authored move placement reported for persistence. */
    std::vector<PluginBlockAssignment> last_move_plugin_placement{};

    /*! \brief Last authored block placement reported for persistence. */
    std::vector<PluginBlockAssignment> last_signal_chain_placement{};

    /*! \brief Last plugin instance selected for a display type override. */
    std::optional<std::string> last_display_type_override_instance_id{};

    /*! \brief Last display type override emitted through the signal-chain panel. */
    std::optional<PluginDisplayType> last_display_type_override{};

    /*! \brief Last plugin instance ID selected for editor-window opening. */
    std::optional<std::string> last_opened_plugin_instance_id{};

    /*! \brief Last unsaved-changes decision emitted by the view. */
    std::optional<UnsavedChangesDecision> last_unsaved_changes_decision{};

    /*! \brief Last interrupted-restore decision emitted by the view. */
    std::optional<RestoreInterruptedDecision> last_restore_interrupted_decision{};

    /*! \brief Number of open intents received. */
    int open_request_count{0};

    /*! \brief Number of import intents received. */
    int import_request_count{0};

    /*! \brief Number of save intents received. */
    int save_request_count{0};

    /*! \brief Number of Save As intents received. */
    int save_as_request_count{0};

    /*! \brief Number of publish intents received. */
    int publish_request_count{0};

    /*! \brief Number of Save As cancellation intents received. */
    int save_as_cancel_count{0};

    /*! \brief Number of busy-operation cancellation intents received. */
    int busy_cancel_request_count{0};

    /*! \brief Number of undo intents received. */
    int undo_request_count{0};

    /*! \brief Number of redo intents received. */
    int redo_request_count{0};

    /*! \brief Number of close intents received. */
    int close_request_count{0};

    /*! \brief Number of exit intents received. */
    int exit_request_count{0};

    /*! \brief Number of unsaved-changes decisions received. */
    int unsaved_changes_decision_count{0};

    /*! \brief Number of interrupted-restore decisions received. */
    int restore_interrupted_decision_count{0};

    /*! \brief Number of play/pause intents received. */
    int play_pause_press_count{0};

    /*! \brief Number of stop intents received. */
    int stop_press_count{0};

    /*! \brief Number of timeline seek intents received. */
    int timeline_seek_count{0};

    /*! \brief Number of grid note-value change intents received. */
    int grid_note_value_change_count{0};

    /*! \brief Counts timeline zoom reports. */
    int timeline_zoom_change_count{0};

    /*! \brief Number of plugin-browser open intents received. */
    int plugin_browser_request_count{0};

    /*! \brief Number of insert-plugin intents received. */
    int plugin_insert_slot_selection_count{0};

    /*! \brief Number of plugin-browser close intents received. */
    int plugin_browser_close_count{0};

    /*! \brief Number of plugin-catalog scan intents received. */
    int plugin_catalog_scan_request_count{0};

    /*! \brief Number of browser plugin-add intents received. */
    int selected_plugin_insert_request_count{0};

    /*! \brief Number of remove-plugin intents received. */
    int remove_plugin_request_count{0};

    /*! \brief Number of move-plugin intents received. */
    int move_plugin_request_count{0};

    /*! \brief Number of signal-chain placement reports received. */
    int signal_chain_placement_change_count{0};

    /*! \brief Number of plugin display type override reports received. */
    int plugin_display_type_override_change_count{0};

    /*! \brief Number of open-plugin intents received. */
    int open_plugin_request_count{0};

    /*! \brief Last input calibration gain value emitted by the calibration popup. */
    std::optional<double> last_input_calibration_gain_db{};

    /*! \brief Last output gain value emitted by the signal-chain panel. */
    std::optional<double> last_output_gain_db{};

    /*! \brief Last output gain preview value emitted by the signal-chain panel. */
    std::optional<double> last_output_gain_preview_db{};

    /*! \brief Number of input calibration intents received. */
    int input_calibration_request_count{0};

    /*! \brief Number of calibration measurement start intents received. */
    int input_calibration_measurement_start_count{0};

    /*! \brief Number of calibration measurement cancellation intents received. */
    int input_calibration_measurement_cancel_count{0};

    /*! \brief Number of successful automatic calibration intents received. */
    int input_calibration_success_count{0};

    /*! \brief Number of successful manual calibration intents received. */
    int input_calibration_manual_set_count{0};

    /*! \brief Number of input calibration dismissed intents received. */
    int input_calibration_dismiss_count{0};

    /*! \brief Number of output gain change intents received. */
    int output_gain_change_count{0};

    /*! \brief Number of output gain preview intents received. */
    int output_gain_preview_change_count{0};

    /*! \brief Last audio-device change callback handed to onAudioDeviceChangeRequested(). */
    std::function<void()> last_audio_device_change{};

    /*! \brief Last post-busy callback handed to onAudioDeviceChangeRequested(). */
    std::function<void()> last_audio_device_after_busy_cleared{};

    /*! \brief Number of audio-device change requests received. */
    int audio_device_change_request_count{0};

    /*! \brief Number of audio-device settings open notifications received. */
    int audio_device_settings_open_count{0};

    /*! \brief Number of audio-device settings close notifications received. */
    int audio_device_settings_close_count{0};
};

} // namespace rock_hero::editor::core::testing
