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
#include <rock_hero/common/audio/input/live_input_monitor_error.h>
#include <rock_hero/common/core/timeline/fraction.h>
#include <rock_hero/common/core/timeline/timeline.h>
#include <rock_hero/common/core/tone/tone_automation.h>
#include <rock_hero/common/core/tone/tone_track.h>
#include <rock_hero/editor/core/audio/game_audio_source_error.h>
#include <rock_hero/editor/core/audio/game_audio_source_state.h>
#include <rock_hero/editor/core/controller/editor_view_state.h>
#include <rock_hero/editor/core/signal_chain/plugin_block_assignment.h>
#include <rock_hero/editor/core/signal_chain/plugin_display_type.h>
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

    /*! \brief Handles a request to start a fresh untitled Tone Designer document. */
    virtual void onNewToneRequested() = 0;

    /*!
    \brief Handles a request to open a tone file as the Tone Designer document.
    \param file Tone file path selected by the user.
    */
    virtual void onOpenToneFileRequested(std::filesystem::path file) = 0;

    /*! \brief Handles a request to save the Tone Designer document to its associated file. */
    virtual void onSaveToneRequested() = 0;

    /*!
    \brief Handles a request to save the Tone Designer document to a chosen tone file.
    \param file Tone file destination path selected by the user.
    */
    virtual void onSaveToneAsRequested(std::filesystem::path file) = 0;

    /*!
    \brief Handles a request to import a tone file into the active project tone.
    \param file Tone file path selected by the user.
    */
    virtual void onImportToneFileRequested(std::filesystem::path file) = 0;

    /*!
    \brief Handles a request to export the active project tone to a tone file.
    \param file Tone file destination path selected by the user.
    */
    virtual void onExportToneFileRequested(std::filesystem::path file) = 0;

    /*!
    \brief Handles the user's decision on the tone-import automation-drop confirmation.
    \param decision User-selected import decision.
    */
    virtual void onToneImportDecision(ToneImportDecision decision) = 0;

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
    \brief Handles a request to change the timeline grid note value.
    \param note_value Grid step as a fraction of a whole note.
    */
    virtual void onGridNoteValueChangeRequested(common::core::Fraction note_value) = 0;

    /*!
    \brief Reports the timeline zoom the view now displays so it can be persisted.

    Zoom is app-local resume state like the cursor position: it never dirties project content and
    is stored per project path outside the package.

    \param pixels_per_second Horizontal timeline scale currently displayed.
    */
    virtual void onTimelineZoomChanged(double pixels_per_second) = 0;

    /*!
    \brief Handles a request to show or hide the waveform behind the tablature lane.

    The preference is app-wide display state: it never dirties project content and is persisted
    outside any package.

    \param visible True when the waveform should draw behind the tablature lane.
    */
    virtual void onWaveformVisibleChangeRequested(bool visible) = 0;

    /*!
    \brief Handles a request to change the minimum number of tablature string lanes displayed.

    The preference is app-wide display state like waveform visibility. The rendered lane count is
    the larger of this minimum and the chart's own string count, so it only ever adds empty lanes.

    \param minimum_strings Minimum lane count; zero means match the chart's string count.
    */
    virtual void onTabMinimumDisplayedStringsChangeRequested(int minimum_strings) = 0;

    /*!
    \brief Handles switching the editor to another arrangement of the loaded song.
    \param arrangement_id Stable arrangement id selected by the user.
    */
    virtual void onArrangementSelected(std::string arrangement_id) = 0;

    /*!
    \brief Handles a deliberate selection of a tone region on the tone track (a click).

    The selection is the Delete target and is drawn with a distinct outline; it is cleared by any
    cursor move. It also becomes the active tone as a preview.

    \param region_id Stable region id, or empty to clear the selection.
    */
    virtual void onToneRegionSelected(std::string region_id) = 0;

    /*!
    \brief Handles the tone row's cursor/playback follow crossing into a new region.

    Makes the region under the cursor the active tone (audible and edited) without formally
    selecting it, and clears any existing selection so a stray Delete cannot remove a tone.
    */
    virtual void onToneRegionActivated() = 0;

    /*!
    \brief Handles a snapped tone-region resize committed by an edge drag.
    \param region_id Stable region id selected by the user.
    \param start New musical start (inclusive).
    \param end New musical end (exclusive).
    */
    virtual void onToneRegionResizeRequested(
        std::string region_id, common::core::GridPosition start,
        common::core::GridPosition end) = 0;

    /*!
    \brief Handles a request to insert a tone-change region at a grid position.

    Splits the region under \p position so the earlier tone runs up to the marker and the new region
    covers the remainder. The new region references an existing catalog tone; minting a fresh tone
    is a separate step the caller performs before issuing this request.

    \param position Grid position at which the tone changes; must fall strictly inside a region.
    \param new_region_id Canonical id minted for the new region beginning at \p position.
    \param tone_document_ref Existing catalog tone the new region references.
    */
    virtual void onToneRegionCreateRequested(
        common::core::GridPosition position, std::string new_region_id,
        std::string tone_document_ref) = 0;

    /*!
    \brief Handles a request to delete a tone region, merging its span into a neighbor.
    \param region_id Stable id of the region to delete.
    */
    virtual void onToneRegionDeleteRequested(std::string region_id) = 0;

    /*!
    \brief Handles a request to rename a tone in the arrangement's tone catalog.

    The name lives on the catalog tone, so every region referencing it relabels together.

    \param tone_document_ref Document ref of the catalog tone to rename.
    \param name New user-facing tone name.
    */
    virtual void onToneRenameRequested(std::string tone_document_ref, std::string name) = 0;

    /*!
    \brief Handles a request to move the shared boundary between two adjacent tone regions.

    Both neighbors move to the new position so gap-free coverage is preserved; the earlier region's
    end and the later region's start are the same boundary.

    \param right_region_id Region on the later side of the boundary (never the first region).
    \param position New grid position for the shared boundary.
    */
    virtual void onToneBoundaryMoveRequested(
        std::string right_region_id, common::core::GridPosition position) = 0;

    /*!
    \brief Handles a request to create a new empty tone at a grid position.

    Mints a fresh empty tone, splits the region under \p position so the part after the marker
    references the new tone, and reloads the rig so the tone becomes audible and editable.

    \param position Grid position at which the new tone begins; must fall strictly inside a region.
    \param name User-facing name for the new tone.
    */
    virtual void onToneCreateNewRequested(
        common::core::GridPosition position, std::string name) = 0;

    /*!
    \brief Handles a request to open an automation lane for a tone-chain plugin parameter.

    Opening a lane authors nothing: the lane tracks the parameter's live value until the first
    point is added, so the sound never changes and the line never surprises. Session-scoped and
    not undoable — an open lane with no points is a view arrangement, not an edit.

    \param instance_id Plugin instance owning the parameter.
    \param param_id Parameter id within the plugin.
    */
    virtual void onToneAutomationLaneAddRequested(
        std::string instance_id, std::string param_id) = 0;

    /*!
    \brief Handles a request to close an automation lane that has no authored points.

    Removes the open lane from the session view; authored lanes are removed by clearing their
    points instead (an undoable edit).

    \param instance_id Plugin instance owning the parameter.
    \param param_id Parameter id within the plugin.
    */
    virtual void onToneAutomationLaneRemoveRequested(
        std::string instance_id, std::string param_id) = 0;

    /*!
    \brief Handles a request to replace a tone-chain plugin parameter's automation points.

    The supplied musical points become the parameter's whole automation (an empty list removes it
    and its lane); the edit is undoable. Positions are exact musical grid positions — the stored
    truth — and the derived playback curve is rewritten from them.

    \param instance_id Plugin instance owning the parameter.
    \param param_id Parameter id within the plugin.
    \param points Replacement points in ascending musical order, values normalised to `[0, 1]`.
    */
    virtual void onSetToneAutomationPoints(
        std::string instance_id, std::string param_id,
        std::vector<common::core::ToneAutomationPoint> points) = 0;

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

    /*!
    \brief Handles a change to the "use game audio settings" toggle.

    Enabling first re-reads the game's configuration fresh; when it is not adoptable (missing or
    uncalibrated) the request is declined with the state-specific reason and **nothing** is
    persisted or changed — the caller reverts its checkbox and shows the carried canonical message.
    Otherwise the toggle is persisted, then (message thread) the store's active source is
    re-selected and the selected route re-applied to the editor engine: enabling adopts the game's
    route, disabling restores the editor's own route. A persisted on therefore always means
    adoption succeeded.

    When the flip resolves to a route that differs from the open device, the blocking re-open runs
    behind the editor's busy overlay, bracketed by \p set_applying (true before, false after it
    clears) so the settings dialog can hide itself exactly as the OK/Cancel apply path does. A
    same-device flip is applied instantly and never invokes \p set_applying. An empty
    \p set_applying runs any re-open inline with no busy presentation; the cancel-time toggle
    restore relies on this because its window is already closing and the cancel's own staged-device
    rollback begins immediately afterwards.

    \param enabled True to source the game's audio configuration, false to source the editor's own.
    \param set_applying Applying presentation bracketing a genuine device re-open, or empty to run
           any re-open inline.
    \return Empty success when the request was applied, or the typed reason the game's audio
            configuration cannot be used (enable requests only; disabling always succeeds).
    */
    [[nodiscard]] virtual std::expected<void, GameAudioSourceError>
    onUseGameAudioSettingsChangeRequested(bool enabled, std::function<void(bool)> set_applying) = 0;

    /*!
    \brief Reads the adoption-readiness of the game's audio configuration, freshly.

    A one-shot read of the game's audio-config file at the moment of the call — the same freshness
    pattern every other decision point uses. The settings window queries this at open time so it
    can disable the "use game audio settings" checkbox when no game configuration exists
    (NotConfigured); Uncalibrated keeps the checkbox clickable so the click can report the
    calibrate-in-game reason.

    \return Adoption-readiness of the game's audio configuration.
    */
    [[nodiscard]] virtual GameAudioSourceState gameAudioSourceState() const = 0;

    /*!
    \brief Handles dismissal of the startup unavailable-game-audio notice.

    Clears the prompt from the view state. The view opens the audio device settings window after
    calling this, so the user lands directly in the editable editor-own flow the fallback selected.
    */
    virtual void onGameAudioUnavailablePromptDismissed() = 0;

    /*!
    \brief Handles the user's response to the startup game-audio recommendation prompt.

    UseGameSettings adopts the game's configuration through the same attempt logic as the settings
    checkbox (the prompt is only raised when adoption can succeed, but a mid-prompt regression
    falls back to the unavailable-game notice). UseCustomSettings persists the toggle off.
    Dismissed persists nothing, so the prompt may re-ask on a later launch.

    \param decision Decision selected by the user.
    \param suppress_future True when "don't show this message again" was checked; persists the
           suppression flag regardless of the decision.
    */
    virtual void onGameAudioRecommendationDecision(
        GameAudioRecommendationDecision decision, bool suppress_future) = 0;

    /*! \brief Handles a request to manually calibrate the current input route. */
    virtual void onInputCalibrationRequested() = 0;

    /*!
    \brief Prepares the live input route for raw input calibration measurement.
    \return Empty success, or a typed live-input failure.
    */
    [[nodiscard]] virtual std::expected<void, common::audio::LiveInputMonitorError>
    onInputCalibrationMeasurementStarted() = 0;

    /*! \brief Stops an active calibration measurement while leaving the prompt open. */
    virtual void onInputCalibrationMeasurementCancelled() = 0;

    /*!
    \brief Applies and stores a completed input calibration gain.
    \param gain_db Calibrated input gain in decibels.
    \return Empty success, or a typed live-input failure.
    */
    [[nodiscard]] virtual std::expected<void, common::audio::LiveInputMonitorError>
    onInputCalibrationSucceeded(double gain_db) = 0;

    /*!
    \brief Applies and stores a manually entered input calibration gain.
    \param gain_db Input gain in decibels.
    \return Empty success, or a typed live-input failure.
    */
    [[nodiscard]] virtual std::expected<void, common::audio::LiveInputMonitorError>
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
