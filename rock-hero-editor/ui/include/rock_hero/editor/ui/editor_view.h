/*!
\file editor_view.h
\brief Concrete JUCE editor view that renders editor state and emits editor intents.
*/

#pragma once

#include <cstdint>
#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>
#include <optional>
#include <rock_hero/common/audio/i_audio_device_configuration.h>
#include <rock_hero/common/audio/i_thumbnail_factory.h>
#include <rock_hero/common/audio/i_transport.h>
#include <rock_hero/common/core/timeline.h>
#include <rock_hero/editor/core/editor_view_state.h>
#include <rock_hero/editor/core/i_editor_controller.h>
#include <rock_hero/editor/core/i_editor_view.h>
#include <rock_hero/editor/ui/arrangement_view.h>
#include <rock_hero/editor/ui/busy_overlay.h>
#include <rock_hero/editor/ui/menu_bar_button.h>
#include <rock_hero/editor/ui/plugin_browser_window.h>
#include <rock_hero/editor/ui/signal_chain_panel.h>
#include <rock_hero/editor/ui/transport_controls.h>
#include <string>

namespace juce
{
// Forward declaration so the settings window can stay out of this public header.
class Component;
} // namespace juce

namespace rock_hero::editor::ui
{

/*!
\brief Computes a cursor x coordinate for a timeline position and visible range.

\param position Current transport position.
\param visible_timeline Visible timeline range.
\param width Drawing width in pixels.
\return Subpixel x coordinate in [0, width - 1], or empty when no cursor can be mapped.
*/
[[nodiscard]] std::optional<float> cursorXForTimelinePosition(
    common::core::TimePosition position, common::core::TimeRange visible_timeline,
    int width) noexcept;

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
                         private SignalChainPanel::Listener,
                         private PluginBrowserWindow::Listener
{
public:
    /*!
    \brief Creates the concrete editor view and installs the thumbnail factory.
    \param controller Controller that receives all user intents emitted by this view.
    \param transport Read-only transport used by cursor drawing and viewport following.
    \param thumbnail_factory Factory used by the arrangement view to create its thumbnail.
    \param audio_devices Optional device-configuration port hosted by the settings window.
    */
    EditorView(
        core::IEditorController& controller, const common::audio::ITransport& transport,
        common::audio::IThumbnailFactory& thumbnail_factory,
        common::audio::IAudioDeviceConfiguration* audio_devices = nullptr);

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
    \brief Handles a selected File-menu item.
    \param menu_item_id Selected menu item identifier.
    \param top_level_menu_index Index of the top-level menu that produced the selection.
    */
    void menuItemSelected(int menu_item_id, int top_level_menu_index) override;

private:
    class CursorOverlay;

    // Private viewport shell that hosts zoomable track content for the editor timeline.
    class TrackViewport;

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

    // Presents, refreshes, or closes the plugin browser window from controller state.
    void presentPluginBrowserIfNeeded(const core::PluginBrowserViewState& state);

    // Returns the editor content area below the menu and transport strips.
    [[nodiscard]] juce::Rectangle<int> trackViewportBounds() const;

    // Positions the menu bar and right-aligned audio-device action across the top strip.
    void layoutMenuStrip();

    // Applies audio routing state to the menu-bar audio-device button.
    void updateAudioDeviceButton();

    // Opens the audio-device settings window.
    void showAudioDeviceSettingsWindow();

    // Posts the pending busy-overlay fence callback after BusyOverlay has painted once.
    void handleBusyOverlayPainted();

    // Queues editor startup focus once JUCE has attached it to a visible peer.
    void requestInitialKeyboardFocusIfReady();

    // TransportControls::Listener implementation.
    void onPlayPausePressed() override;

    // TransportControls::Listener implementation.
    void onStopPressed() override;

    // SignalChainPanel::Listener implementation.
    void onAddPluginPressed() override;

    // SignalChainPanel::Listener implementation.
    void onRemovePluginPressed(std::string instance_id) override;

    // SignalChainPanel::Listener implementation.
    void onOpenPluginPressed(std::string instance_id) override;

    // PluginBrowserWindow::Listener implementation.
    void onPluginBrowserScanRequested() override;

    // PluginBrowserWindow::Listener implementation.
    void onPluginBrowserAddRequested(std::string plugin_id) override;

    // PluginBrowserWindow::Listener implementation.
    void onPluginBrowserClosed() override;

    // Controller that owns editor workflow policy.
    core::IEditorController& m_controller;

    // Audio-device configuration backend hosted by the settings window; null when unavailable.
    common::audio::IAudioDeviceConfiguration* m_audio_devices{nullptr};

    // Last state pushed by the controller; used for load target lookup and layout mapping.
    core::EditorViewState m_state{};

    // Flat app-menu look-and-feel owned for the lifetime of the menu bar.
    std::unique_ptr<juce::LookAndFeel> m_menu_look_and_feel;

    // Editor File menu.
    juce::MenuBarComponent m_menu_bar;

    // Concrete presentation-only transport control strip.
    TransportControls m_transport_controls;

    // Right-aligned menu-bar action that opens audio-device settings.
    MenuBarButton m_audio_device_button;

    // Bottom control panel for the plugin chain.
    SignalChainPanel m_signal_chain_panel;

    // Waveform track for the currently displayed arrangement, hosted inside the track viewport.
    ArrangementView m_arrangement_view;

    // Editor-wide cursor and seek overlay drawn above the zoomable track canvas.
    std::unique_ptr<CursorOverlay> m_cursor_overlay;

    // Real viewport that hosts the zoomable track canvas.
    std::unique_ptr<TrackViewport> m_track_viewport;

    // Owned asynchronous file chooser; must outlive the native dialog callback.
    std::unique_ptr<juce::FileChooser> m_file_chooser;

    // Optional top-level plugin browser window.
    std::unique_ptr<PluginBrowserWindow> m_plugin_browser_window;

    // Editor-wide busy overlay rendered on top of the editor content during slow operations.
    BusyOverlay m_busy_overlay;

    // Pending single-shot callback waiting for the next busy overlay paint.
    std::function<void()> m_after_busy_overlay_paint;

    // Last unsaved-changes prompt already shown to avoid re-opening dialogs on repeated pushes.
    std::optional<core::UnsavedChangesPrompt> m_last_presented_unsaved_changes_prompt{};

    // Last Save As prompt already shown to avoid re-opening choosers on repeated pushes.
    std::optional<core::SaveAsPrompt> m_last_presented_save_as_prompt{};

    // True after the editor has made its one startup focus request.
    bool m_has_requested_initial_keyboard_focus{false};
};

} // namespace rock_hero::editor::ui
