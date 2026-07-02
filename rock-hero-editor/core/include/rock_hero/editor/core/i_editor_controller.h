/*!
\file i_editor_controller.h
\brief Framework-free editor controller contract.
*/

#pragma once

#include <cstddef>
#include <expected>
#include <filesystem>
#include <functional>
#include <optional>
#include <rock_hero/common/audio/live_input_error.h>
#include <rock_hero/common/core/fraction.h>
#include <rock_hero/common/core/timeline.h>
#include <rock_hero/editor/core/editor_view_state.h>
#include <rock_hero/editor/core/plugin_block_assignment.h>
#include <rock_hero/editor/core/plugin_display_type.h>
#include <string>
#include <vector>

namespace rock_hero::editor::core
{

/*!
\brief Project-owned boundary for editor user intents.

Concrete implementations translate these plain-English intents into transport, audio, session, and
view updates without exposing JUCE callback types to tests or to non-UI code.
*/
class IEditorController
{
public:
    /*! \brief Destroys the editor-controller interface. */
    virtual ~IEditorController() = default;

    /*!
    \brief Handles a request to open an editor project package.
    \param file Filesystem path selected by the user.
    */
    virtual void onOpenRequested(std::filesystem::path file) = 0;

    /*!
    \brief Handles a request to import a song source.
    \param file Filesystem path selected by the user.
    */
    virtual void onImportRequested(std::filesystem::path file) = 0;

    /*! \brief Handles a request to save to the current destination. */
    virtual void onSaveRequested() = 0;

    /*!
    \brief Handles a request to save to a chosen destination.
    \param file Filesystem path selected by the user.
    */
    virtual void onSaveAsRequested(std::filesystem::path file) = 0;

    /*!
    \brief Handles a request to publish a native song package.
    \param file Filesystem path selected by the user.
    */
    virtual void onPublishRequested(std::filesystem::path file) = 0;

    /*! \brief Handles cancellation of a controller-requested Save As destination chooser. */
    virtual void onSaveAsCancelled() = 0;

    /*! \brief Handles a request to cancel the active cancellable busy operation. */
    virtual void onBusyCancelRequested() = 0;

    /*! \brief Handles a request to undo the most recent editor history entry. */
    virtual void onUndoRequested() = 0;

    /*! \brief Handles a request to redo the next editor history entry. */
    virtual void onRedoRequested() = 0;

    /*! \brief Handles a request to close the current project without exiting the app. */
    virtual void onCloseRequested() = 0;

    /*! \brief Handles a request to exit the editor application. */
    virtual void onExitRequested() = 0;

    /*!
    \brief Handles the user's response to an unsaved-changes confirmation prompt.
    \param decision Decision selected by the user.
    */
    virtual void onUnsavedChangesDecision(UnsavedChangesDecision decision) = 0;

    /*!
    \brief Handles the user's response to a previous interrupted startup restore.
    \param decision Decision selected by the user.
    */
    virtual void onRestoreInterruptedDecision(RestoreInterruptedDecision decision) = 0;

    /*! \brief Handles a play/pause button press from the editor UI. */
    virtual void onPlayPausePressed() = 0;

    /*! \brief Handles a stop button press from the editor UI. */
    virtual void onStopPressed() = 0;

    /*!
    \brief Handles a timeline seek request at an absolute timeline position.
    \param position Requested seek position on the song timeline.
    */
    virtual void onTimelineSeekRequested(common::core::TimePosition position) = 0;

    /*!
    \brief Handles a request to change the timeline grid spacing.
    \param spacing_beats Grid step measured in tempo-map beats.
    */
    virtual void onGridSpacingChangeRequested(common::core::Fraction spacing_beats) = 0;

    /*! \brief Handles a request to show the scanned plugin browser. */
    virtual void onPluginBrowserRequested() = 0;

    /*!
    \brief Handles selection of a signal-chain slot where a plugin may be inserted.
    \param chain_index User-visible insertion slot while the current chain has capacity.
    \param block_index Fixed visual block the inserted plugin should occupy.
    */
    virtual void onPluginInsertSlotSelected(std::size_t chain_index, std::size_t block_index) = 0;

    /*! \brief Handles the plugin browser window closing. */
    virtual void onPluginBrowserClosed() = 0;

    /*! \brief Handles a request to rescan the plugin browser catalog. */
    virtual void onPluginCatalogScanRequested() = 0;

    /*!
    \brief Handles a request to insert the selected plugin from the browser.
    \param plugin_id Opaque plugin ID selected by the user.
    */
    virtual void onSelectedPluginInsertRequested(std::string plugin_id) = 0;

    /*!
    \brief Handles a request to remove a plugin instance from the plugin chain.
    \param instance_id Opaque plugin instance ID selected by the user.
    */
    virtual void onRemovePluginRequested(std::string instance_id) = 0;

    /*!
    \brief Handles a request to move a plugin instance within the plugin chain.
    \param instance_id Opaque plugin instance ID selected by the user.
    \param destination_index Final user-visible chain index for the instance.
    \param placement Fixed visual block assignments after the move.
    */
    virtual void onMovePluginRequested(
        std::string instance_id, std::size_t destination_index,
        std::vector<PluginBlockAssignment> placement) = 0;

    /*!
    \brief Reports the editor-authored visual block placement so it persists with the project.

    The view owns the transient gesture math and emits the committed result here whenever a
    placement-only edit lands. The controller stores it as the authoritative placement and writes it
    on the next capture, keeping save/reload faithful to the on-screen gap arrangement.

    \param placement Fixed visual block assignments for current plugin instances.
    */
    virtual void onSignalChainPlacementChanged(std::vector<PluginBlockAssignment> placement) = 0;

    /*!
    \brief Handles a manual display type override for a plugin instance.
    \param instance_id Opaque plugin instance ID selected by the user.
    \param display_type Manual display type, or empty to use automatic classification.
    */
    virtual void onPluginDisplayTypeOverrideChanged(
        std::string instance_id, std::optional<PluginDisplayType> display_type) = 0;

    /*!
    \brief Handles a request to open a plugin instance editor window.
    \param instance_id Opaque plugin instance ID selected by the user.
    */
    virtual void onOpenPluginRequested(std::string instance_id) = 0;

    /*! \brief Handles a request to manually calibrate the current input route. */
    virtual void onInputCalibrationRequested() = 0;

    /*!
    \brief Prepares the live input route for raw input calibration measurement.
    \return Empty success, or a typed live-input failure.
    */
    [[nodiscard]] virtual std::expected<void, common::audio::LiveInputError>
    onInputCalibrationMeasurementStarted() = 0;

    /*! \brief Stops an active calibration measurement while leaving the prompt open. */
    virtual void onInputCalibrationMeasurementCancelled() = 0;

    /*!
    \brief Applies and stores a completed input calibration gain.
    \param gain_db Calibrated input gain in decibels.
    \return Empty success, or a typed live-input failure.
    */
    [[nodiscard]] virtual std::expected<void, common::audio::LiveInputError>
    onInputCalibrationSucceeded(double gain_db) = 0;

    /*!
    \brief Applies and stores a manually entered input calibration gain.
    \param gain_db Input gain in decibels.
    \return Empty success, or a typed live-input failure.
    */
    [[nodiscard]] virtual std::expected<void, common::audio::LiveInputError>
    onInputCalibrationManuallySet(double gain_db) = 0;

    /*! \brief Handles the calibration prompt closing without a new successful calibration. */
    virtual void onInputCalibrationDismissed() = 0;

    /*!
    \brief Handles a preview-only output gain change while the user is dragging the slider.
    \param gain_db Desired output gain in decibels.
    */
    virtual void onOutputGainPreviewChanged(double gain_db) = 0;

    /*!
    \brief Handles a committed change to the output gain slider.
    \param gain_db Desired output gain in decibels.
    */
    virtual void onOutputGainChanged(double gain_db) = 0;

    /*!
    \brief Schedules audio-device open work behind the editor's busy overlay.

    The supplied work callable runs after the busy overlay paints, so the user sees a static
    blocking indicator while the underlying device-manager call occupies the message thread. The
    after-cleared callable runs only after the editor controller has cleared the surrounding busy
    presentation.

    \param change_audio_device Callable run after the busy overlay paints; must be safe to call
    once.
    \param after_busy_cleared Callable run after the busy overlay clears; must be safe to call
    once.
    */
    virtual void onAudioDeviceChangeRequested(
        std::function<void()> change_audio_device, std::function<void()> after_busy_cleared) = 0;

    /*!
    \brief Requests opening the audio-device settings window.
    \return True when the caller may open the settings window and must later call
    onAudioDeviceSettingsClosed() exactly once; false when the request is refused, e.g. while the
    input calibration prompt is active, and the caller must not open the window.
    */
    [[nodiscard]] virtual bool onAudioDeviceSettingsOpenRequested() = 0;

    /*! \brief Handles the audio-device settings window closing. */
    virtual void onAudioDeviceSettingsClosed() = 0;

protected:
    /*! \brief Creates the editor-controller interface. */
    IEditorController() = default;

    /*! \brief Copies the editor-controller interface. */
    IEditorController(const IEditorController&) = default;

    /*! \brief Moves the editor-controller interface. */
    IEditorController(IEditorController&&) = default;

    /*!
    \brief Assigns the editor-controller interface from another interface.
    \return Reference to this editor-controller interface.
    */
    IEditorController& operator=(const IEditorController&) = default;

    /*!
    \brief Move-assigns the editor-controller interface from another interface.
    \return Reference to this editor-controller interface.
    */
    IEditorController& operator=(IEditorController&&) = default;
};

} // namespace rock_hero::editor::core
