/*!
\file editor_view.h
\brief Concrete JUCE editor view that renders editor state and emits editor intents.
*/

#pragma once

#include "busy/busy_overlay.h"
#include "main_window/menu_bar_button.h"
#include "main_window/undo_history_overlay.h"
#include "shared/audio_level_meter.h"
#include "signal_chain/plugin_browser_window.h"
#include "signal_chain/signal_chain_panel.h"
#include "tab/tab_view.h"
#include "timeline/arrangement_view.h"
#include "timeline/grid_spacing_selector.h"
#include "tone/tone_automation_lanes_view.h"
#include "tone/tone_track_view.h"
#include "transport/transport_controls.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>
#include <optional>
#include <rock_hero/common/audio/automation/i_tone_automation.h>
#include <rock_hero/common/audio/clock/i_playback_clock.h>
#include <rock_hero/common/audio/device/i_audio_device_configuration.h>
#include <rock_hero/common/audio/input/i_audio_meter_source.h>
#include <rock_hero/common/audio/input/i_live_input.h>
#include <rock_hero/common/audio/song/i_thumbnail_factory.h>
#include <rock_hero/common/audio/transport/i_transport.h>
#include <rock_hero/common/core/timeline/fraction.h>
#include <rock_hero/editor/core/controller/editor_view_state.h>
#include <rock_hero/editor/core/controller/i_editor_controller.h>
#include <rock_hero/editor/core/controller/i_editor_view.h>
#include <string>
#include <vector>

namespace juce
{
// Forward declaration so the settings window can stay out of this public header.
class Component;
} // namespace juce

namespace rock_hero::editor::ui
{

// Forward declarations so the timeline cursor overlay, viewport, calibration, and preview window
// units stay out of this header.
class CursorOverlay;
class InputCalibrationWindow;
class PreviewWindow;
class TrackViewport;

/*!
\brief JUCE implementation of the editor view contract.

EditorView renders transition-shaped editor state, owns concrete child widgets, and forwards user
intent to the editor controller. It also owns one editor-wide cursor overlay that reads current
position through a const common::audio::ITransport reference at vblank cadence; current cursor
position is not part of the derived editor state.
*/
class EditorView final : public juce::Component,
                         public juce::MenuBarModel,
                         public core::IEditorView,
                         private TransportControls::Listener,
                         private GridSpacingSelector::Listener,
                         private SignalChainView::Listener,
                         private ToneTrackView::Listener,
                         private ToneAutomationLanesView::Listener,
                         private PluginBrowserWindow::Listener
{
public:
    /*!
    \brief Audio ports consumed directly by the concrete view.

    EditorView receives only display and popup-hosting ports. Workflow mutations stay behind the
    controller.
    */
    struct AudioPorts final
    {
        /*! \brief Read-only transport used by cursor drawing and viewport following. */
        const common::audio::ITransport& transport;

        /*! \brief Playback-time telemetry sampled by the 3D preview while playing (plan 44). */
        const common::audio::IPlaybackClock& playback_clock;

        /*! \brief Factory used by the arrangement view to create its thumbnail. */
        common::audio::IThumbnailFactory& thumbnail_factory;

        /*! \brief Audio-device configuration port hosted by the settings window. */
        common::audio::IAudioDeviceConfiguration& audio_devices;

        /*! \brief Meter source sampled for continuous level display. */
        const common::audio::IAudioMeterSource& meter_source;

        /*! \brief Live-input source sampled by the calibration popup. */
        const common::audio::ILiveInput& live_input;

        /*! \brief Automation port polled read-only by live-tracking automation lanes. */
        const common::audio::IToneAutomation& tone_automation;
    };

    /*!
    \brief Creates the concrete editor view and installs the thumbnail factory.
    \param controller Controller that receives all user intents emitted by this view.
    \param audio_ports Required audio ports consumed directly by this view.
    */
    EditorView(core::IEditorController& controller, AudioPorts audio_ports);

    /*! \brief Releases child widgets, cursor overlay, and project chooser state. */
    ~EditorView() override;

    /*! \brief Copying is disabled because JUCE component ownership is not copyable. */
    EditorView(const EditorView&) = delete;

    /*! \brief Copy assignment is disabled because JUCE component ownership is not copyable. */
    EditorView& operator=(const EditorView&) = delete;

    /*! \brief Moving is disabled because child component registrations are not movable. */
    EditorView(EditorView&&) = delete;

    /*! \brief Move assignment is disabled because child registrations are not movable. */
    EditorView& operator=(EditorView&&) = delete;

    /*!
    \brief Applies transition-shaped editor state to child widgets.
    \param state State derived by the controller.
    */
    void setState(const core::EditorViewState& state) override;

    /*!
    \brief Presents a transient editor workflow error.
    \param message User-facing error message.
    */
    void showError(const std::string& message) override;

    /*!
    \brief Runs a callback after the busy overlay paints.
    \param callback Callback to run after the overlay paint fence is crossed.
    */
    void runAfterBusyOverlayPainted(std::function<void()> callback) override;

    /*!
    \brief Runs a callback after the editor repaints without the busy overlay.
    \param callback Callback to run after the overlay removal paint fence is crossed.
    */
    void runAfterBusyOverlayRemoved(std::function<void()> callback) override;

    /*!
    \brief Paints the editor background behind child widgets.
    \param g Graphics context used for drawing.
    */
    void paint(juce::Graphics& g) override;

    /*! \brief Lays out the menu, transport controls, timeline viewport, and signal-chain panel. */
    void resized() override;

    /*! \brief Requests startup keyboard focus when the editor becomes visible. */
    void visibilityChanged() override;

    /*! \brief Requests startup keyboard focus when the editor is attached to a window. */
    void parentHierarchyChanged() override;

    /*!
    \brief Handles editor-level keyboard shortcuts.
    \param key Key press delivered by JUCE.
    \return True when the shortcut was handled.
    */
    bool keyPressed(const juce::KeyPress& key) override;

    /*!
    \brief Returns the top-level editor menu names.
    \return Menu names shown by the menu bar.
    */
    [[nodiscard]] juce::StringArray getMenuBarNames() override;

    /*!
    \brief Builds one top-level menu from the current editor state.
    \param top_level_menu_index Index of the requested top-level menu.
    \param menu_name Name of the requested top-level menu.
    \return Popup menu for the requested menu.
    */
    [[nodiscard]] juce::PopupMenu getMenuForIndex(
        int top_level_menu_index, const juce::String& menu_name) override;

    /*!
    \brief Handles a selected editor menu item.
    \param menu_item_id Selected menu item identifier.
    \param top_level_menu_index Index of the top-level menu that produced the selection.
    */
    void menuItemSelected(int menu_item_id, int top_level_menu_index) override;

private:
    enum class SaveAsChooserPurpose : std::uint8_t
    {
        UserSaveAs,
        DeferredAction,
    };

    // Opens the asynchronous project package chooser and forwards accepted selections.
    void showOpenChooser();

    // Opens the asynchronous import chooser and forwards accepted selections.
    void showImportChooser();

    // Opens the asynchronous save chooser and forwards accepted selections.
    void showSaveAsChooser(SaveAsChooserPurpose purpose);

    // Opens the asynchronous publish chooser and forwards accepted selections.
    void showPublishChooser();

    // Presents an unsaved-changes prompt once per prompt request.
    void presentUnsavedChangesPromptIfNeeded(
        const std::optional<core::UnsavedChangesPrompt>& prompt);

    // Presents a Save As chooser once per controller-requested prompt.
    void presentSaveAsPromptIfNeeded(const std::optional<core::SaveAsPrompt>& prompt);

    // Presents an interrupted startup-restore prompt once per prompt request.
    void presentRestoreInterruptedPromptIfNeeded(
        const std::optional<core::RestoreInterruptedPrompt>& prompt);

    // Presents the startup unavailable-game-audio notice once per prompt request, opening the
    // audio device settings window on dismissal.
    void presentGameAudioUnavailablePromptIfNeeded(
        const std::optional<core::GameAudioUnavailablePrompt>& prompt);

    // Opens or releases the startup game-audio recommendation dialog from controller state.
    void presentGameAudioRecommendationIfNeeded(bool prompt_requested);

    // Presents or closes the input calibration prompt from controller state.
    void presentInputCalibrationPromptIfNeeded(
        const std::optional<core::InputCalibrationPrompt>& prompt);

    // Presents, refreshes, or closes the plugin browser window from controller state.
    void presentPluginBrowserIfNeeded(const core::PluginBrowserViewState& state);

    // Shows or hides the undo-history inspector panel (View > Undo History, or F8).
    void toggleUndoHistoryPanel();

    // Returns the editor content area below the menu and transport strips.
    [[nodiscard]] juce::Rectangle<int> trackViewportBounds() const;

    // Positions the menu bar and right-aligned audio-device action across the top strip.
    void layoutMenuStrip();

    // Applies audio routing state to the menu-bar audio-device button.
    void updateAudioDeviceButton();

    // Samples continuous meter display data from the audio backend.
    void refreshAudioMeters();

    // Samples the transport cursor position into the transport-strip time and musical readouts.
    void refreshTimeDisplay();

    // Opens the audio-device settings window.
    void showAudioDeviceSettingsWindow();

    // Defers settings-window destruction until the current close callback stack unwinds.
    void scheduleAudioDeviceSettingsWindowReset();

    // Posts the pending busy-overlay fence callback after BusyOverlay has painted once.
    void handleBusyOverlayPainted();

    // Posts the pending busy-clear fence callback after EditorView paints without the overlay.
    void handleBusyOverlayRemovedPainted();

    // Queues editor startup focus once JUCE has attached it to a visible peer.
    void requestInitialKeyboardFocusIfReady();

    // TransportControls::Listener implementation.
    void onPlayPausePressed() override;

    // TransportControls::Listener implementation.
    void onStopPressed() override;

    // GridSpacingSelector::Listener implementation.
    void onGridNoteValueChosen(common::core::Fraction note_value) override;

    // SignalChainView::Listener implementation.
    void onInsertPluginPressed(std::size_t chain_index, std::size_t block_index) override;

    // SignalChainView::Listener implementation.
    void onRemovePluginPressed(std::string instance_id) override;

    // SignalChainView::Listener implementation.
    void onMovePluginPressed(
        std::string instance_id, std::size_t destination_index,
        std::vector<core::PluginBlockAssignment> placement) override;

    // SignalChainView::Listener implementation.
    void onSignalChainPlacementChanged(std::vector<core::PluginBlockAssignment> placement) override;

    // SignalChainView::Listener implementation.
    void onPluginDisplayTypeOverrideChanged(
        std::string instance_id, std::optional<core::PluginDisplayType> display_type) override;

    // SignalChainView::Listener implementation.
    void onOpenPluginPressed(std::string instance_id) override;

    // SignalChainView::Listener implementation.
    void onInputCalibrationPressed() override;

    // SignalChainView::Listener implementation.
    void onOutputGainPreviewChanged(double gain_db) override;

    // SignalChainView::Listener implementation.
    void onOutputGainChanged(double gain_db) override;

    /*! \copydoc ToneTrackView::Listener::onToneRegionSelected */
    void onToneRegionSelected(std::string region_id) override;

    /*! \copydoc ToneTrackView::Listener::onToneRegionActivated */
    void onToneRegionActivated() override;

    /*! \brief Shows the tone-picker menu to insert a tone-change marker at the playhead. */
    void createToneMarkerAtPlayhead();

    /*! \brief Shows the tone-picker menu to insert a tone-change marker at a musical position. */
    void createToneMarkerAt(common::core::GridPosition position);

    /*! \brief Prompts for a name and asks the controller to create a new tone at the marker. */
    void promptForNewTone(common::core::GridPosition position);

    /*! \brief Shows a modal single-field text prompt, invoking on_accept with the entered text. */
    void promptForText(
        const juce::String& title, const juce::String& message, const juce::String& initial_value,
        const juce::String& accept_label, std::function<void(const juce::String&)> on_accept);

    /*! \copydoc ToneTrackView::Listener::onToneBoundaryMoveRequested */
    void onToneBoundaryMoveRequested(
        std::string right_region_id, common::core::GridPosition position) override;

    /*! \copydoc ToneTrackView::Listener::onToneRenamePromptRequested */
    void onToneRenamePromptRequested(
        std::string tone_document_ref, std::string current_name) override;

    /*! \copydoc ToneTrackView::Listener::onToneRegionResizeRequested */
    void onToneRegionResizeRequested(
        std::string region_id, common::core::GridPosition start,
        common::core::GridPosition end) override;

    /*! \copydoc ToneTrackView::Listener::onToneChangeInsertRequested */
    void onToneChangeInsertRequested(common::core::GridPosition position) override;

    /*! \copydoc ToneTrackView::Listener::onToneRegionDeleteRequested */
    void onToneRegionDeleteRequested(std::string region_id) override;

    /*! \copydoc ToneAutomationLanesView::Listener::onToneAutomationLaneAddRequested */
    void onToneAutomationLaneAddRequested(std::string instance_id, std::string param_id) override;

    /*! \copydoc ToneAutomationLanesView::Listener::onToneAutomationLaneRemoveRequested */
    void onToneAutomationLaneRemoveRequested(
        std::string instance_id, std::string param_id) override;

    /*! \copydoc ToneAutomationLanesView::Listener::onToneAutomationPointsEditRequested */
    void onToneAutomationPointsEditRequested(
        std::string instance_id, std::string param_id,
        std::vector<common::core::ToneAutomationPoint> points) override;

    // PluginBrowserWindow::Listener implementation.
    void onPluginBrowserScanRequested() override;

    // PluginBrowserWindow::Listener implementation.
    void onPluginBrowserAddRequested(std::string plugin_id) override;

    // PluginBrowserWindow::Listener implementation.
    void onPluginBrowserClosed() override;

    // PluginBrowserWindow::Listener implementation.
    void onPluginBrowserBusyCancelRequested() override;

    // Controller that owns editor workflow policy.
    core::IEditorController& m_controller;

    // Audio-device configuration backend hosted by the settings window.
    common::audio::IAudioDeviceConfiguration& m_audio_devices;

    // Read-only meter source sampled at display cadence.
    const common::audio::IAudioMeterSource& m_audio_meters;

    // Live-input source sampled by the calibration popup.
    const common::audio::ILiveInput& m_live_input;

    // Read-only transport sampled at display cadence for the transport-strip time readout.
    const common::audio::ITransport& m_transport;

    // Playback-time telemetry the 3D preview samples while playing (plan 44).
    const common::audio::IPlaybackClock& m_playback_clock;

    // Shows or hides the 3D preview window (View > 3D Preview, or F3).
    void togglePreviewWindow();

    // Last state pushed by the controller; used for load target lookup and layout mapping.
    // Updates the top-level window title to reflect the open project name, REAPER-style.
    void updateWindowTitle();

    core::EditorViewState m_state{};

    // Flat app-menu look-and-feel owned for the lifetime of the menu bar.
    std::unique_ptr<juce::LookAndFeel> m_menu_look_and_feel;

    // Editor File menu.
    juce::MenuBarComponent m_menu_bar;

    // Concrete presentation-only transport control strip.
    TransportControls m_transport_controls;

    // Read-only readout of the transport cursor's current position as
    // "measure.beat.hundredths / time" (plain time without a project), shown beside the
    // transport buttons and refreshed at display cadence. The active tempo and time signature
    // are not repeated here: the timeline ruler's header bands pin them to their left edge.
    juce::Label m_position_display;

    // Transport position behind the current m_position_display text, so vblank frames where the
    // transport holds position skip re-formatting the readout strings. Seconds alone identify
    // the text because the other readout inputs (project-loaded flag, tempo map) change only
    // through setState, which clears this cache.
    std::optional<double> m_position_readout_seconds{};

    // Timeline grid-size selector shown beside the transport controls.
    GridSpacingSelector m_grid_spacing_selector;

    // Transport-bar meter for the final mix output.
    AudioLevelMeter m_master_output_meter;

    // Right-aligned menu-bar action that opens audio-device settings.
    MenuBarButton m_audio_device_button;

    // Caption naming the arrangement dropdown, mirroring the grid selector's caption band.
    juce::Label m_arrangement_caption;

    // Dropdown that switches the displayed arrangement, pinned left of the grid selector.
    juce::ComboBox m_arrangement_selector;

    // Bottom control panel for the plugin chain.
    SignalChainPanel m_signal_chain_panel;

    // Waveform track for the currently displayed arrangement, hosted inside the track viewport.
    ArrangementView m_arrangement_view;

    // Tablature lane drawn over the waveform row inside the track viewport.
    TabView m_tab_view;

    // Tone track row hosted below the waveform inside the track viewport.
    ToneTrackView m_tone_track_view;

    // Automation lanes row hosted below the tone track inside the track viewport.
    ToneAutomationLanesView m_tone_automation_lanes_view;

    // Editor-wide cursor and seek overlay drawn above the zoomable track canvas.
    std::unique_ptr<CursorOverlay> m_cursor_overlay;

    // Real viewport that hosts the zoomable track canvas.
    std::unique_ptr<TrackViewport> m_track_viewport;

    // Overlay listing the full undo/redo stack in real time; hidden until toggled (View > Undo
    // History, or F8).
    UndoHistoryOverlay m_undo_history_overlay;

    // Owned asynchronous file chooser; must outlive the native dialog callback.
    std::unique_ptr<juce::FileChooser> m_file_chooser;

    // Optional top-level plugin browser window.
    std::unique_ptr<PluginBrowserWindow> m_plugin_browser_window;

    // Optional top-level input calibration window. Concrete type so its read-only game-reflection
    // mode can be re-scoped live when the "use game audio settings" toggle flips while it is open.
    std::unique_ptr<InputCalibrationWindow> m_input_calibration_window;

    // Optional top-level audio-device settings window.
    std::unique_ptr<juce::DocumentWindow> m_audio_device_settings_window;

    // Optional top-level 3D preview window (plan 44); created on first toggle, then kept and
    // shown/hidden (its render surface rebuilds per open).
    std::unique_ptr<PreviewWindow> m_preview_window;

    // True once the settings window has reported close and is waiting for deferred destruction.
    bool m_audio_device_settings_window_reset_pending{false};

    // Editor-wide busy overlay rendered on top of the editor content during slow operations.
    BusyOverlay m_busy_overlay;

    // Vblank callback used for continuous meter and time-readout refresh without controller
    // state churn.
    juce::VBlankAttachment m_meter_vblank_attachment;

    // Pending single-shot callback waiting for the next busy overlay paint.
    std::function<void()> m_after_busy_overlay_paint;

    // Pending single-shot callback waiting for the first repaint after busy overlay removal.
    std::function<void()> m_after_busy_overlay_removed_paint;

    // Last unsaved-changes prompt already shown to avoid re-opening dialogs on repeated pushes.
    std::optional<core::UnsavedChangesPrompt> m_last_presented_unsaved_changes_prompt{};

    // Last Save As prompt already shown to avoid re-opening choosers on repeated pushes.
    std::optional<core::SaveAsPrompt> m_last_presented_save_as_prompt{};

    // Last restore-interrupted prompt shown to avoid re-opening dialogs on repeated pushes.
    std::optional<core::RestoreInterruptedPrompt> m_last_presented_restore_interrupted_prompt{};

    // Last unavailable-game-audio notice shown to avoid re-opening dialogs on repeated pushes.
    std::optional<core::GameAudioUnavailablePrompt> m_last_game_audio_unavailable_prompt{};

    // True while the controller's current recommendation request has been presented; the
    // self-deleting standard alert owns its own teardown, so only the dedup flag lives here.
    bool m_game_audio_recommendation_presented{false};

    // True after the editor has made its one startup focus request.
    bool m_has_requested_initial_keyboard_focus{false};
};

} // namespace rock_hero::editor::ui
