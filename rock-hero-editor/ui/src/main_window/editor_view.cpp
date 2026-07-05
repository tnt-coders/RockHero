#include "editor_view.h"

#include "audio_device/audio_device_settings_window.h"
#include "input_calibration/input_calibration_window.h"
#include "main_window/menu_look_and_feel.h"
#include "shared/editor_colors.h"
#include "timeline/cursor_overlay.h"
#include "timeline/track_viewport.h"

#include <algorithm>
#include <filesystem>
#include <memory>
#include <optional>
#include <rock_hero/common/audio/song/i_thumbnail.h>
#include <rock_hero/common/core/shared/juce_path.h>
#include <rock_hero/common/core/song/audio_asset.h>
#include <rock_hero/common/core/timeline/tempo_map.h>
#include <rock_hero/editor/core/timeline/transport_readout_text.h>
#include <string>
#include <utility>
#include <vector>

namespace rock_hero::editor::ui
{

namespace
{

constexpr int g_open_command{1};
constexpr int g_import_command{2};
constexpr int g_save_command{3};
constexpr int g_save_as_command{4};
constexpr int g_close_command{5};
constexpr int g_exit_command{6};
constexpr int g_publish_command{7};
constexpr int g_undo_command{101};
constexpr int g_redo_command{102};
constexpr int g_menu_bar_height{24};
constexpr int g_content_inset{8};
constexpr int g_control_gap{8};
// Sized for a long readout like "128.4.99 / 59:59:999" at the readout font height.
constexpr int g_position_display_width{240};
constexpr float g_transport_readout_font_height{20.0f};
constexpr int g_transport_height{32};
constexpr int g_transport_bar_height{g_content_inset + g_transport_height};
constexpr int g_transport_controls_width{96};
constexpr int g_grid_spacing_selector_width{132};
constexpr int g_master_meter_width{384};
constexpr int g_master_meter_min_width{196};
// Floor wide enough to fit the closed-device sentinel without truncation; ceiling chosen so the
// File/Edit/... menu titles still have room on the smallest supported window width.
constexpr int g_audio_device_menu_button_min_width{180};
constexpr int g_audio_device_menu_button_max_width{520};
constexpr int g_signal_chain_panel_min_height{160};
constexpr int g_signal_chain_panel_max_height{260};
constexpr int g_track_viewport_min_height{80};

const juce::Colour g_transport_bar_color{juce::Colours::darkgrey.darker(0.16f)};

// Reserves enough right-side menu space for the current audio status without overlapping menus.
[[nodiscard]] int audioDeviceButtonWidth(
    const MenuBarButton& button, int menu_bar_height, int available_width)
{
    const int preferred_width = std::clamp(
        button.preferredWidthForHeight(menu_bar_height),
        g_audio_device_menu_button_min_width,
        g_audio_device_menu_button_max_width);
    return std::min(preferred_width, std::max(0, available_width));
}

// Ensures saved project packages use the Rock Hero project extension when needed.
[[nodiscard]] std::filesystem::path pathWithRhpExtension(const juce::File& file)
{
    std::filesystem::path path = common::core::pathFromJuceFile(file);
    if (!path.empty() && path.extension().empty())
    {
        path.replace_extension(".rhp");
    }
    return path;
}

// Ensures published song packages use the native Rock Hero song extension when needed.
[[nodiscard]] std::filesystem::path pathWithRockExtension(const juce::File& file)
{
    std::filesystem::path path = common::core::pathFromJuceFile(file);
    if (!path.empty() && path.extension().empty())
    {
        path.replace_extension(".rock");
    }
    return path;
}

// Converts a project-suggested publish path into JUCE's save-dialog starting file.
[[nodiscard]] juce::File publishChooserInitialFile(const std::filesystem::path& suggested_file)
{
    if (suggested_file.empty())
    {
        return juce::File::getSpecialLocation(juce::File::userHomeDirectory);
    }

    return common::core::juceFileFromPath(suggested_file);
}

[[nodiscard]] juce::String editCommandText(
    const char* command_name, const std::optional<std::string>& label)
{
    if (!label.has_value() || label->empty())
    {
        return command_name;
    }

    return juce::String{command_name} + " " + juce::String{label->c_str()};
}

[[nodiscard]] int normalizedAsciiKeyCode(int key_code) noexcept
{
    if (key_code >= 'A' && key_code <= 'Z')
    {
        return key_code - 'A' + 'a';
    }
    return key_code;
}

[[nodiscard]] bool hasCommandShortcutModifier(const juce::KeyPress& key) noexcept
{
    const juce::ModifierKeys modifiers = key.getModifiers();
    return modifiers.isCommandDown() && !modifiers.isAltDown();
}

[[nodiscard]] bool isUndoShortcut(const juce::KeyPress& key) noexcept
{
    return hasCommandShortcutModifier(key) && !key.getModifiers().isShiftDown() &&
           normalizedAsciiKeyCode(key.getKeyCode()) == 'z';
}

[[nodiscard]] bool isRedoShortcut(const juce::KeyPress& key) noexcept
{
    return hasCommandShortcutModifier(key) && !key.getModifiers().isShiftDown() &&
           normalizedAsciiKeyCode(key.getKeyCode()) == 'y';
}

// Requests project-load cursor focus once per accepted load, without inferring "a load happened"
// from busy phases or timeline diffs. The controller bumps project_load_id only when an open,
// restore, or import completes successfully, so a change in that id (or a project first appearing)
// marks a fresh load. A failed open leaves the id unchanged, and ordinary edits keep the same id,
// so neither recenters.
[[nodiscard]] bool shouldFocusCursorAfterStateChange(
    const core::EditorViewState& previous_state, const core::EditorViewState& next_state)
{
    if (!next_state.project_loaded || next_state.busy.has_value())
    {
        return false;
    }

    return !previous_state.project_loaded ||
           next_state.project_load_id != previous_state.project_load_id;
}

// Gives the unsaved-changes prompt enough context for the action that triggered it. Only the
// deferrable subset reaches this switch; the post-switch return is a defensive fallback for ids the
// controller cannot legitimately stash in a deferred slot.
[[nodiscard]] juce::String unsavedChangesPromptMessage(core::EditorActionId action)
{
    switch (action)
    {
        case core::EditorActionId::CloseProject:
        {
            return "Save changes before closing the current project?";
        }
        case core::EditorActionId::OpenProject:
        {
            return "Save changes before opening another project?";
        }
        case core::EditorActionId::RestoreProject:
        {
            return "Save changes before restoring the previous project?";
        }
        case core::EditorActionId::ImportSong:
        {
            return "Save changes before importing another project?";
        }
        case core::EditorActionId::ExitApplication:
        {
            return "Save changes before exiting Rock Hero Editor?";
        }
        case core::EditorActionId::SaveProject:
        case core::EditorActionId::SaveProjectAs:
        case core::EditorActionId::PublishProject:
        case core::EditorActionId::ResolveUnsavedChangesPrompt:
        case core::EditorActionId::CancelSaveAsPrompt:
        case core::EditorActionId::CancelBusyOperation:
        case core::EditorActionId::Undo:
        case core::EditorActionId::Redo:
        case core::EditorActionId::PlayPause:
        case core::EditorActionId::Stop:
        case core::EditorActionId::SeekTimeline:
        case core::EditorActionId::SetGridNoteValue:
        case core::EditorActionId::ShowPluginBrowser:
        case core::EditorActionId::BeginPluginInsert:
        case core::EditorActionId::ScanPluginCatalog:
        case core::EditorActionId::InsertSelectedPlugin:
        case core::EditorActionId::RemovePlugin:
        case core::EditorActionId::MovePlugin:
        case core::EditorActionId::SetSignalChainPlacement:
        case core::EditorActionId::SetPluginDisplayTypeOverride:
        case core::EditorActionId::OpenPlugin:
        case core::EditorActionId::SelectToneRegion:
        case core::EditorActionId::ResizeToneRegion:
        {
            return "Save changes before continuing?";
        }
    }

    return "Save changes before continuing?";
}

} // namespace

// Creates child widgets and gives the arrangement view its waveform-thumbnail factory.
EditorView::EditorView(core::IEditorController& controller, AudioPorts audio_ports)
    : m_controller(controller)
    , m_audio_devices(audio_ports.audio_devices)
    , m_audio_meters(audio_ports.meter_source)
    , m_live_input(audio_ports.live_input)
    , m_transport(audio_ports.transport)
    , m_menu_look_and_feel(std::make_unique<MenuLookAndFeel>())
    , m_menu_bar(this)
    , m_transport_controls(*this)
    , m_grid_spacing_selector(*this)
    , m_master_output_meter(AudioLevelMeterOrientation::Horizontal, "Master")
    , m_signal_chain_panel(*this)
    , m_tone_track_view(*this, m_state.tempo_map)
    , m_cursor_overlay(
          std::make_unique<CursorOverlay>(controller, audio_ports.transport, m_state.tempo_map))
    , m_track_viewport(
          std::make_unique<TrackViewport>(
              controller, m_arrangement_view, m_tone_track_view, *m_cursor_overlay,
              audio_ports.transport))
    , m_meter_vblank_attachment(this, [this] {
        refreshAudioMeters();
        refreshTimeDisplay();
    })
{
    setWantsKeyboardFocus(true);

    m_menu_bar.setComponentID("file_menu_bar");
    m_menu_bar.setLookAndFeel(m_menu_look_and_feel.get());
    m_transport_controls.setComponentID("transport_controls");
    m_position_display.setComponentID("transport_position_display");
    // Large bold digits keep the readout legible at a glance next to the transport buttons.
    m_position_display.setFont(
        juce::Font{juce::FontOptions{g_transport_readout_font_height, juce::Font::bold}});
    m_position_display.setJustificationType(juce::Justification::centredLeft);
    m_position_display.setInterceptsMouseClicks(false, false);
    refreshTimeDisplay();
    m_master_output_meter.setComponentID("master_output_meter");
    m_audio_device_button.setComponentID("audio_device_button");
    m_audio_device_button.setText("Audio Device");
    m_audio_device_button.onClick = [this] { showAudioDeviceSettingsWindow(); };
    m_arrangement_view.setComponentID("arrangement_view");
    m_tone_track_view.setComponentID("tone_track_view");
    m_tone_track_view.setSnapGuideCallback([this](std::optional<TimelineSnapGuide> guide) {
        m_cursor_overlay->setSnapGuide(std::move(guide));
    });
    m_cursor_overlay->setHitTestPassThrough([this](juce::Point<int> position) {
        const juce::Rectangle<int> row_bounds = m_tone_track_view.getBounds();
        return row_bounds.contains(position) &&
               m_tone_track_view.wantsPointerAt(position - row_bounds.getPosition());
    });
    m_busy_overlay.setComponentID("busy_overlay");
    m_busy_overlay.setPaintCallback([this] { handleBusyOverlayPainted(); });
    m_busy_overlay.setCancelCallback([this] { m_controller.onBusyCancelRequested(); });

    m_arrangement_view.setThumbnailFactory(audio_ports.thumbnail_factory);

    addAndMakeVisible(m_menu_bar);
    addAndMakeVisible(m_transport_controls);
    addAndMakeVisible(m_position_display);
    addAndMakeVisible(m_grid_spacing_selector);
    addAndMakeVisible(m_master_output_meter);
    addAndMakeVisible(m_audio_device_button);
    addAndMakeVisible(m_signal_chain_panel);
    addAndMakeVisible(*m_track_viewport);
    // BusyOverlay is added last so it lands on top of the editor child stack. It also calls
    // toFront() on activation, but adding it here as the final child means the initial Z-order
    // is already correct before the first push.
    addChildComponent(m_busy_overlay);
    m_track_viewport->setProjectLoaded(m_state.project_loaded);
    // Zoom is app-local resume state like the cursor; the controller persists it per project.
    m_track_viewport->setZoomChangedCallback([this](double pixels_per_second) {
        m_controller.onTimelineZoomChanged(pixels_per_second);
    });

    setSize(1280, 800);
}

// Disconnects the menu bar from this model before base and member teardown begins.
EditorView::~EditorView()
{
    if (m_audio_device_settings_window != nullptr && !m_audio_device_settings_window_reset_pending)
    {
        m_controller.onAudioDeviceSettingsClosed();
    }

    m_audio_device_settings_window.reset();
    m_audio_device_settings_window_reset_pending = false;
    m_busy_overlay.setPaintCallback({});
    m_busy_overlay.setCancelCallback({});
    m_menu_bar.setLookAndFeel(nullptr);
    m_menu_bar.setModel(nullptr);
}

// Projects controller-derived state into child widgets and cursor mapping state.
void EditorView::setState(const core::EditorViewState& state)
{
    const core::EditorViewState previous_state = m_state;
    m_state = state;
    if (!m_state.busy.has_value())
    {
        m_after_busy_overlay_paint = {};
    }

    menuItemsChanged();
    m_track_viewport->setProjectLoaded(m_state.project_loaded);
    m_track_viewport->setTimelineRange(m_state.visible_timeline);
    m_track_viewport->setTransportDisplayState(
        m_state.transport.play_pause_shows_pause_icon, m_state.transport.stop_enabled);
    m_track_viewport->setGrid(m_state.tempo_map, m_state.grid_note_value);
    if (shouldFocusCursorAfterStateChange(previous_state, m_state))
    {
        // Restored zoom applies before the recenter so centering math uses the restored scale.
        if (m_state.timeline_zoom_pixels_per_second > 0.0)
        {
            m_track_viewport->setRestoredZoomPixelsPerSecond(
                m_state.timeline_zoom_pixels_per_second);
        }
        m_track_viewport->requestCursorFocus();
    }
    m_transport_controls.setState(m_state.transport);
    m_grid_spacing_selector.setNoteValue(m_state.grid_note_value);
    m_grid_spacing_selector.setEnabled(m_state.project_loaded);
    updateAudioDeviceButton();
    m_signal_chain_panel.setState(m_state.signal_chain);
    refreshAudioMeters();
    // Project load/close swaps the tempo map behind the musical readout, and the vblank feed
    // only runs while the view is showing, so state pushes invalidate the cached readout sample
    // and refresh the readouts directly.
    m_position_readout_seconds.reset();
    refreshTimeDisplay();

    m_arrangement_view.setVisibleTimeline(m_state.visible_timeline);
    m_arrangement_view.setState(m_state.arrangement);

    m_tone_track_view.setVisibleTimeline(m_state.visible_timeline);
    m_tone_track_view.setState(m_state.tone_track);

    m_cursor_overlay->setVisibleTimelineRange(m_state.visible_timeline);
    m_cursor_overlay->setGridNoteValue(m_state.grid_note_value);
    presentUnsavedChangesPromptIfNeeded(m_state.unsaved_changes_prompt);
    presentSaveAsPromptIfNeeded(m_state.save_as_prompt);
    presentRestoreInterruptedPromptIfNeeded(m_state.restore_interrupted_prompt);
    presentInputCalibrationPromptIfNeeded(m_state.input_calibration_prompt);
    presentPluginBrowserIfNeeded(m_state.plugin_browser);
    m_busy_overlay.setBusyState(m_state.busy);
    repaint();
}

// Presents controller-reported workflow failures as one-shot dialogs.
void EditorView::showError(const std::string& message)
{
    juce::NativeMessageBox::showAsync(
        juce::MessageBoxOptions()
            .withIconType(juce::MessageBoxIconType::WarningIcon)
            .withTitle("Could not complete request")
            .withMessage(juce::String{message.c_str()})
            .withButton("OK"),
        nullptr);
}

// Defers message-thread-only work until BusyOverlay has actually rendered the busy state.
void EditorView::runAfterBusyOverlayPainted(std::function<void()> callback)
{
    if (!m_state.busy.has_value())
    {
        m_after_busy_overlay_paint = {};
        return;
    }

    m_after_busy_overlay_paint = std::move(callback);
    if (m_after_busy_overlay_paint)
    {
        // Startup restore can request the fence before the window has a paintable peer.
        // Waiting for an impossible paint would leave the project-open continuation stuck.
        if (!isShowing())
        {
            // Clear the member before invoking so reentrant fence requests see no stale callback.
            const std::function<void()> pending_callback = std::move(m_after_busy_overlay_paint);
            m_after_busy_overlay_paint = {};
            pending_callback();
            return;
        }

        m_busy_overlay.repaint();
    }
}

// Defers follow-up work until the editor has presented one frame without the busy overlay.
void EditorView::runAfterBusyOverlayRemoved(std::function<void()> callback)
{
    if (!callback)
    {
        return;
    }

    m_after_busy_overlay_removed_paint = std::move(callback);
    if (!isShowing())
    {
        // Headless tests and startup teardown cannot produce a native repaint. Run now so the
        // workflow never waits indefinitely for a presentation that cannot happen.
        const std::function<void()> pending_callback =
            std::move(m_after_busy_overlay_removed_paint);
        m_after_busy_overlay_removed_paint = {};
        pending_callback();
        return;
    }

    repaint();
}

// Paints the background and transport strip behind child widgets.
void EditorView::paint(juce::Graphics& g)
{
    g.fillAll(g_editor_background_color);

    g.setColour(g_transport_bar_color);
    g.fillRect(0, g_menu_bar_height, getWidth(), g_transport_bar_height);
    handleBusyOverlayRemovedPainted();
}

// Keeps the control strip above the timeline viewport and signal-chain panel.
void EditorView::resized()
{
    layoutMenuStrip();
    auto top_area = getLocalBounds();
    top_area.removeFromTop(g_menu_bar_height);
    auto transport_row = top_area.removeFromTop(g_transport_bar_height);
    auto control_row =
        transport_row.withTrimmedLeft(g_content_inset).withTrimmedRight(g_content_inset);
    control_row = control_row.withSizeKeepingCentre(
        control_row.getWidth(), std::min(g_transport_height, control_row.getHeight()));

    // The grid selector pins to the strip's left edge. The playback block — transport buttons
    // plus the position readout — centers as a whole on the WINDOW's center line. The block only
    // slides off center when the grid selector would overlap it, and the master meter takes
    // whatever right-edge width the block leaves free (hiding below its minimum), so the
    // centered block wins over the meter's preferred width. The readout slot width already
    // includes the g_content_inset its bounds trim off, so the block width is a plain sum.
    m_grid_spacing_selector.setBounds(control_row.removeFromLeft(
        std::min(g_grid_spacing_selector_width, control_row.getWidth())));

    constexpr int playback_group_width = g_transport_controls_width + g_position_display_width;
    const int ideal_group_x = getLocalBounds().getCentreX() - playback_group_width / 2;
    const int rightmost_group_x =
        std::max(control_row.getX(), control_row.getRight() - playback_group_width);
    const int group_x = std::clamp(ideal_group_x, control_row.getX(), rightmost_group_x);
    juce::Rectangle<int> playback_area{
        group_x,
        control_row.getY(),
        std::min(playback_group_width, control_row.getRight() - group_x),
        control_row.getHeight(),
    };

    const int master_meter_width = std::min(
        g_master_meter_width, control_row.getRight() - playback_area.getRight() - g_content_inset);
    if (master_meter_width >= g_master_meter_min_width)
    {
        m_master_output_meter.setVisible(true);
        m_master_output_meter.setBounds(
            control_row.removeFromRight(master_meter_width).reduced(0, 4));
    }
    else
    {
        m_master_output_meter.setVisible(false);
        m_master_output_meter.setBounds({});
    }

    m_transport_controls.setBounds(playback_area.removeFromLeft(
        std::min(g_transport_controls_width, playback_area.getWidth())));
    m_position_display.setBounds(
        playback_area.removeFromLeft(std::min(g_position_display_width, playback_area.getWidth()))
            .withTrimmedLeft(g_content_inset));

    auto bottom_area = trackViewportBounds();
    const int target_signal_chain_panel_height = std::clamp(
        bottom_area.getHeight() / 3,
        g_signal_chain_panel_min_height,
        g_signal_chain_panel_max_height);
    const int max_signal_chain_panel_height =
        std::max(0, bottom_area.getHeight() - g_control_gap - g_track_viewport_min_height);
    const int signal_chain_panel_height =
        std::min(target_signal_chain_panel_height, max_signal_chain_panel_height);
    auto signal_chain_panel_bounds = bottom_area.removeFromBottom(signal_chain_panel_height);
    if (signal_chain_panel_height > 0)
    {
        bottom_area.removeFromBottom(std::min(g_control_gap, bottom_area.getHeight()));
    }
    m_track_viewport->setBounds(bottom_area);
    m_signal_chain_panel.setBounds(signal_chain_panel_bounds);
    m_busy_overlay.setBounds(getLocalBounds());
}

// Retries the startup focus request if this component is explicitly shown later.
void EditorView::visibilityChanged()
{
    requestInitialKeyboardFocusIfReady();
}

// Retries the startup focus request when JUCE attaches the editor under a window peer.
void EditorView::parentHierarchyChanged()
{
    requestInitialKeyboardFocusIfReady();
}

// Routes editor-level keyboard shortcuts through the same controller intents as child widgets.
bool EditorView::keyPressed(const juce::KeyPress& key)
{
    if (isUndoShortcut(key))
    {
        if (!m_state.undo_enabled)
        {
            return false;
        }

        m_controller.onUndoRequested();
        return true;
    }

    if (isRedoShortcut(key))
    {
        if (!m_state.redo_enabled)
        {
            return false;
        }

        m_controller.onRedoRequested();
        return true;
    }

    if (key == juce::KeyPress{juce::KeyPress::spaceKey})
    {
        m_controller.onPlayPausePressed();
        return true;
    }

    return false;
}

// Returns the top-level editor menus displayed by the editor.
juce::StringArray EditorView::getMenuBarNames()
{
    return {"File", "Edit"};
}

// Builds menus using only controller-derived state.
juce::PopupMenu EditorView::getMenuForIndex(int top_level_menu_index, const juce::String& menu_name)
{
    if (top_level_menu_index == 0 && menu_name == "File")
    {
        juce::PopupMenu menu;
        menu.addItem(g_open_command, "Open...", m_state.open_enabled);
        menu.addItem(g_import_command, "Import...", m_state.import_enabled);
        menu.addSeparator();
        menu.addItem(g_save_command, "Save", m_state.save_enabled);
        menu.addItem(g_save_as_command, "Save As...", m_state.save_as_enabled);
        menu.addItem(g_publish_command, "Publish...", m_state.publish_enabled);
        menu.addSeparator();
        menu.addItem(g_close_command, "Close", m_state.close_enabled);
        menu.addItem(g_exit_command, "Exit");
        return menu;
    }

    if (top_level_menu_index == 1 && menu_name == "Edit")
    {
        juce::PopupMenu menu;
        menu.addItem(
            g_undo_command, editCommandText("Undo", m_state.undo_label), m_state.undo_enabled);
        menu.addItem(
            g_redo_command, editCommandText("Redo", m_state.redo_label), m_state.redo_enabled);
        return menu;
    }

    return {};
}

// Routes menu selections to either a chooser or a direct controller intent.
void EditorView::menuItemSelected(int menu_item_id, int /*top_level_menu_index*/)
{
    switch (menu_item_id)
    {
        case g_undo_command:
        {
            if (m_state.undo_enabled)
            {
                m_controller.onUndoRequested();
            }
            break;
        }
        case g_redo_command:
        {
            if (m_state.redo_enabled)
            {
                m_controller.onRedoRequested();
            }
            break;
        }
        case g_open_command:
        {
            if (m_state.open_enabled)
            {
                showOpenChooser();
            }
            break;
        }
        case g_import_command:
        {
            if (m_state.import_enabled)
            {
                showImportChooser();
            }
            break;
        }
        case g_save_command:
        {
            if (!m_state.save_enabled)
            {
                break;
            }
            if (m_state.save_requires_destination)
            {
                showSaveAsChooser(SaveAsChooserPurpose::UserSaveAs);
            }
            else
            {
                m_controller.onSaveRequested();
            }
            break;
        }
        case g_save_as_command:
        {
            if (m_state.save_as_enabled)
            {
                showSaveAsChooser(SaveAsChooserPurpose::UserSaveAs);
            }
            break;
        }
        case g_publish_command:
        {
            if (m_state.publish_enabled)
            {
                showPublishChooser();
            }
            break;
        }
        case g_close_command:
        {
            if (m_state.close_enabled)
            {
                m_controller.onCloseRequested();
            }
            break;
        }
        case g_exit_command:
        {
            m_controller.onExitRequested();
            break;
        }
        default:
        {
            break;
        }
    }
}

// Opens an asynchronous file chooser and sends accepted project package paths to the controller.
void EditorView::showOpenChooser()
{
    m_file_chooser = std::make_unique<juce::FileChooser>(
        "Open Rock Hero Project",
        juce::File::getSpecialLocation(juce::File::userHomeDirectory),
        "*.rhp");

    const juce::Component::SafePointer<EditorView> safe_this{this};
    m_file_chooser->launchAsync(
        juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [safe_this](const juce::FileChooser& chooser) {
            if (safe_this == nullptr)
            {
                return;
            }

            const auto file = chooser.getResult();
            if (!file.existsAsFile())
            {
                return;
            }

            safe_this->m_controller.onOpenRequested(common::core::pathFromJuceFile(file));
        });
}

// Opens an asynchronous file chooser and sends accepted import paths to the controller.
void EditorView::showImportChooser()
{
    m_file_chooser = std::make_unique<juce::FileChooser>(
        "Import Rock Hero Song",
        juce::File::getSpecialLocation(juce::File::userHomeDirectory),
        "*.rock");

    const juce::Component::SafePointer<EditorView> safe_this{this};
    m_file_chooser->launchAsync(
        juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [safe_this](const juce::FileChooser& chooser) {
            if (safe_this == nullptr)
            {
                return;
            }

            const auto file = chooser.getResult();
            if (!file.existsAsFile())
            {
                return;
            }

            safe_this->m_controller.onImportRequested(common::core::pathFromJuceFile(file));
        });
}

// Opens an asynchronous file chooser and sends accepted save paths to the controller.
void EditorView::showSaveAsChooser(SaveAsChooserPurpose purpose)
{
    m_file_chooser = std::make_unique<juce::FileChooser>(
        "Save Rock Hero Project",
        juce::File::getSpecialLocation(juce::File::userHomeDirectory),
        "*.rhp");

    const juce::Component::SafePointer<EditorView> safe_this{this};
    m_file_chooser->launchAsync(
        juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles |
            juce::FileBrowserComponent::warnAboutOverwriting,
        [safe_this, purpose](const juce::FileChooser& chooser) {
            if (safe_this == nullptr)
            {
                return;
            }

            const auto file = chooser.getResult();
            if (file.getFullPathName().isEmpty())
            {
                if (purpose == SaveAsChooserPurpose::DeferredAction)
                {
                    safe_this->m_controller.onSaveAsCancelled();
                }
                return;
            }

            safe_this->m_controller.onSaveAsRequested(pathWithRhpExtension(file));
        });
}

// Opens an asynchronous file chooser and sends accepted native song package paths to the
// controller.
void EditorView::showPublishChooser()
{
    m_file_chooser = std::make_unique<juce::FileChooser>(
        "Publish Rock Hero Song (.rock)",
        publishChooserInitialFile(m_state.suggested_publish_file),
        "*.rock");

    const juce::Component::SafePointer<EditorView> safe_this{this};
    m_file_chooser->launchAsync(
        juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles |
            juce::FileBrowserComponent::warnAboutOverwriting,
        [safe_this](const juce::FileChooser& chooser) {
            if (safe_this == nullptr)
            {
                return;
            }

            const auto file = chooser.getResult();
            if (file.getFullPathName().isEmpty())
            {
                return;
            }

            safe_this->m_controller.onPublishRequested(pathWithRockExtension(file));
        });
}

// Shows each distinct unsaved-changes prompt once and reports the selected decision.
void EditorView::presentUnsavedChangesPromptIfNeeded(
    const std::optional<core::UnsavedChangesPrompt>& prompt)
{
    if (!prompt.has_value())
    {
        m_last_presented_unsaved_changes_prompt.reset();
        return;
    }

    if (m_last_presented_unsaved_changes_prompt == prompt)
    {
        return;
    }

    m_last_presented_unsaved_changes_prompt = prompt;
    const juce::Component::SafePointer<EditorView> safe_this{this};
    juce::NativeMessageBox::showAsync(
        juce::MessageBoxOptions()
            .withIconType(juce::MessageBoxIconType::QuestionIcon)
            .withTitle("Unsaved changes")
            .withMessage(unsavedChangesPromptMessage(prompt->prompted_action))
            .withButton("Save")
            .withButton("Discard")
            .withButton("Cancel")
            .withAssociatedComponent(this),
        [safe_this](int button_index) {
            if (safe_this == nullptr)
            {
                return;
            }

            switch (button_index)
            {
                case 0:
                {
                    safe_this->m_controller.onUnsavedChangesDecision(
                        core::UnsavedChangesDecision::Save);
                    break;
                }
                case 1:
                {
                    safe_this->m_controller.onUnsavedChangesDecision(
                        core::UnsavedChangesDecision::Discard);
                    break;
                }
                default:
                {
                    safe_this->m_controller.onUnsavedChangesDecision(
                        core::UnsavedChangesDecision::Cancel);
                    break;
                }
            }
        });
}

// Shows a controller-requested Save As chooser once and reports cancellation when needed.
void EditorView::presentSaveAsPromptIfNeeded(const std::optional<core::SaveAsPrompt>& prompt)
{
    if (!prompt.has_value())
    {
        m_last_presented_save_as_prompt.reset();
        return;
    }

    if (m_last_presented_save_as_prompt == prompt)
    {
        return;
    }

    m_last_presented_save_as_prompt = prompt;
    showSaveAsChooser(SaveAsChooserPurpose::DeferredAction);
}

// Shows each distinct interrupted-restore prompt once and reports Retry as the standard OK button.
void EditorView::presentRestoreInterruptedPromptIfNeeded(
    const std::optional<core::RestoreInterruptedPrompt>& prompt)
{
    if (!prompt.has_value())
    {
        m_last_presented_restore_interrupted_prompt.reset();
        return;
    }

    if (m_last_presented_restore_interrupted_prompt == prompt)
    {
        return;
    }

    m_last_presented_restore_interrupted_prompt = prompt;
    const juce::Component::SafePointer<EditorView> safe_this{this};
    juce::NativeMessageBox::showAsync(
        juce::MessageBoxOptions()
            .withIconType(juce::MessageBoxIconType::QuestionIcon)
            .withTitle("Project did not finish opening")
            .withMessage(
                juce::String{"The previous project did not finish opening:\n\n"} +
                common::core::juceStringFromPath(prompt->project_file) +
                "\n\nTry opening it again?")
            .withButton("OK")
            .withButton("Cancel")
            .withAssociatedComponent(this),
        [safe_this](int button_index) {
            if (safe_this == nullptr)
            {
                return;
            }

            const core::RestoreInterruptedDecision decision =
                button_index == 0 ? core::RestoreInterruptedDecision::Retry
                                  : core::RestoreInterruptedDecision::Cancel;
            safe_this->m_controller.onRestoreInterruptedDecision(decision);
        });
}

// Opens or closes the input calibration prompt from controller-derived state.
void EditorView::presentInputCalibrationPromptIfNeeded(
    const std::optional<core::InputCalibrationPrompt>& prompt)
{
    if (!prompt.has_value())
    {
        if (m_input_calibration_window != nullptr)
        {
            m_input_calibration_window->setVisible(false);

            const juce::Component::SafePointer<EditorView> safe_this{this};
            juce::MessageManager::callAsync([safe_this] {
                EditorView* const view = safe_this.getComponent();
                // The calibration window can request this from its own timer or close callback.
                // Hide now, but defer destruction until that event stack has unwound.
                if (view != nullptr && !view->m_state.input_calibration_prompt.has_value())
                {
                    view->m_input_calibration_window.reset();
                }
            });
        }
        return;
    }

    if (m_input_calibration_window != nullptr)
    {
        m_input_calibration_window->toFront(true);
        return;
    }

    m_input_calibration_window = std::make_unique<InputCalibrationWindow>(
        m_controller, &m_live_input, *prompt, isShowing() ? this : nullptr);
}

// Opens or refreshes the plugin browser top-level window from controller-derived state.
void EditorView::presentPluginBrowserIfNeeded(const core::PluginBrowserViewState& state)
{
    if (!state.visible)
    {
        if (m_plugin_browser_window != nullptr)
        {
            m_plugin_browser_window->setVisible(false);

            const juce::Component::SafePointer<EditorView> safe_this{this};
            juce::MessageManager::callAsync([safe_this] {
                EditorView* const view = safe_this.getComponent();
                // The browser can request this while JUCE is dispatching its own close event.
                // Hide now, but defer destruction until that event stack has unwound.
                // This is a Component lifetime guard: SafePointer is JUCE's built-in way for
                // posted UI callbacks to observe deletion without keeping the Component alive.
                if (view != nullptr && !view->m_state.plugin_browser.visible)
                {
                    view->m_plugin_browser_window.reset();
                }
            });
        }
        return;
    }

    if (m_plugin_browser_window == nullptr)
    {
        auto& listener = static_cast<PluginBrowserWindow::Listener&>(*this);
        m_plugin_browser_window = std::make_unique<PluginBrowserWindow>(listener);
        if (isShowing())
        {
            m_plugin_browser_window->centreAroundComponent(this, 760, 500);
        }
    }

    m_plugin_browser_window->setVisible(true);
    m_plugin_browser_window->setState(state);
    m_plugin_browser_window->setBusyState(m_state.busy);
    m_plugin_browser_window->toFront(true);
}

// Re-positions only the menu-bar children when the audio-device label changes width, so a
// status-text update does not relayout the transport row, track viewport, or signal-chain panel.
void EditorView::layoutMenuStrip()
{
    juce::Rectangle<int> menu_bar_bounds = getLocalBounds().removeFromTop(g_menu_bar_height);
    const juce::Rectangle<int> audio_device_bounds =
        menu_bar_bounds.removeFromRight(audioDeviceButtonWidth(
            m_audio_device_button, g_menu_bar_height, menu_bar_bounds.getWidth()));
    m_menu_bar.setBounds(menu_bar_bounds);
    m_audio_device_button.setBounds(audio_device_bounds);
}

// Applies controller-derived audio routing state to the menu-bar button.
void EditorView::updateAudioDeviceButton()
{
    const juce::String status_text{m_state.audio_device_status_text.c_str()};
    if (m_audio_device_button.getText() != status_text)
    {
        m_audio_device_button.setText(status_text);
        layoutMenuStrip();
    }

    m_audio_device_button.setEnabled(
        m_state.audio_devices_available && m_state.audio_device_settings_enabled);
}

// Samples meter values at display cadence. This intentionally bypasses EditorController state
// because meters are volatile playback display data, like cursor position.
void EditorView::refreshAudioMeters()
{
    const common::audio::AudioMeterSnapshot snapshot = m_audio_meters.audioMeterSnapshot();
    m_master_output_meter.setLevel(snapshot.master_output);
    m_signal_chain_panel.setMeterLevels(snapshot.live_rig_input, snapshot.live_rig_output);
}

// Samples the transport cursor into the strip readout at display cadence, like the cursor
// overlay. Vblank frames where the transport holds position return before any string formatting;
// setState clears the cached sample so a swapped tempo map or project-loaded flag re-renders
// even at an unchanged position. The beat position stays off without a project because the
// default tempo map would show placeholder values; the plain time remains meaningful either way.
void EditorView::refreshTimeDisplay()
{
    const double seconds = m_transport.position().seconds;
    if (m_position_readout_seconds == seconds)
    {
        return;
    }

    m_position_readout_seconds = seconds;
    std::string readout = core::timelineTimeText(seconds);
    if (m_state.project_loaded)
    {
        readout = core::beatPositionText(m_state.tempo_map, seconds) + " / " + readout;
    }

    m_position_display.setText(juce::String{readout}, juce::dontSendNotification);
}

// Opens the audio-device settings window when a hardware-configuration backend is available.
void EditorView::showAudioDeviceSettingsWindow()
{
    if (!m_state.audio_device_settings_enabled)
    {
        return;
    }

    if (m_audio_device_settings_window != nullptr)
    {
        m_audio_device_settings_window->toFront(true);
        return;
    }

    // Hand the dispatcher to the settings window so OK and Cancel can dismiss the dialog
    // immediately and run device-manager work behind the editor's blocking busy overlay.
    // juce::AudioDeviceManager occupies the message thread, so the overlay's blocking
    // presentation paints once before the freeze rather than animating through it.
    const juce::Component::SafePointer<EditorView> safe_this{this};
    if (!m_controller.onAudioDeviceSettingsOpenRequested())
    {
        return;
    }

    m_audio_device_settings_window_reset_pending = false;
    m_audio_device_settings_window = AudioDeviceSettingsWindow::show(
        m_audio_devices,
        m_audio_device_button,
        [safe_this](std::function<void()> operation, std::function<void()> after_cleared) {
            if (auto* view = safe_this.getComponent())
            {
                view->m_controller.onAudioDeviceChangeRequested(
                    std::move(operation), std::move(after_cleared));
                return;
            }

            if (operation)
            {
                operation();
            }
            if (after_cleared)
            {
                after_cleared();
            }
        },
        [safe_this] {
            if (auto* view = safe_this.getComponent())
            {
                view->m_audio_device_settings_window_reset_pending = true;
                view->m_controller.onAudioDeviceSettingsClosed();
                view->scheduleAudioDeviceSettingsWindowReset();
            }
        });
}

// Clears the owner-held settings window after JUCE and view callbacks have unwound.
void EditorView::scheduleAudioDeviceSettingsWindowReset()
{
    const juce::Component::SafePointer<EditorView> safe_this{this};
    juce::MessageManager::callAsync([safe_this] {
        if (auto* view = safe_this.getComponent())
        {
            view->m_audio_device_settings_window.reset();
            view->m_audio_device_settings_window_reset_pending = false;
        }
    });
}

// Runs the single pending fence callback after BusyOverlay has crossed its paint path. Showing
// views post a follow-up message so expensive work stays out of the real paint callback.
void EditorView::handleBusyOverlayPainted()
{
    if (!m_after_busy_overlay_paint)
    {
        return;
    }

    std::function<void()> callback = std::move(m_after_busy_overlay_paint);
    m_after_busy_overlay_paint = {};
    if (!isShowing())
    {
        callback();
        return;
    }

    juce::MessageManager::callAsync(std::move(callback));
}

// Runs the single pending clear-fence callback after the editor has painted with busy cleared.
void EditorView::handleBusyOverlayRemovedPainted()
{
    if (m_state.busy.has_value() || !m_after_busy_overlay_removed_paint)
    {
        return;
    }

    std::function<void()> callback = std::move(m_after_busy_overlay_removed_paint);
    m_after_busy_overlay_removed_paint = {};
    juce::MessageManager::callAsync(std::move(callback));
}

// Returns the area shared by the track viewport and bottom signal-chain panel.
juce::Rectangle<int> EditorView::trackViewportBounds() const
{
    auto area = getLocalBounds();
    area.removeFromTop(g_menu_bar_height);
    area.removeFromTop(g_transport_bar_height);
    area.removeFromTop(g_control_gap);
    area.removeFromLeft(g_content_inset);
    area.removeFromRight(g_content_inset);
    area.removeFromBottom(g_content_inset);
    return area;
}

// Schedules focus after the current attach/show callback so the native peer can activate first.
void EditorView::requestInitialKeyboardFocusIfReady()
{
    if (m_has_requested_initial_keyboard_focus || !isShowing())
    {
        return;
    }

    m_has_requested_initial_keyboard_focus = true;
    const juce::Component::SafePointer<EditorView> safe_this{this};
    juce::MessageManager::callAsync([safe_this] {
        if (safe_this == nullptr)
        {
            return;
        }

        if (!safe_this->isShowing())
        {
            safe_this->m_has_requested_initial_keyboard_focus = false;
            return;
        }

        safe_this->grabKeyboardFocus();
    });
}

// Forwards the transport-control intent to the workflow controller.
void EditorView::onPlayPausePressed()
{
    m_controller.onPlayPausePressed();
}

// Forwards the stop-control intent to the workflow controller.
void EditorView::onStopPressed()
{
    m_controller.onStopPressed();
}

// Forwards the raw note value; the controller owns validation, and a rejected entry needs no
// feedback because the selector already reverted its display to the last applied value.
void EditorView::onGridNoteValueChosen(common::core::Fraction note_value)
{
    m_controller.onGridNoteValueChangeRequested(note_value);
}

// Opens the plugin browser for a specific insertion slot selected in the signal-chain panel.
void EditorView::onInsertPluginPressed(std::size_t chain_index, std::size_t block_index)
{
    if (!m_state.signal_chain.insert_plugin_enabled)
    {
        return;
    }

    m_controller.onPluginInsertSlotSelected(chain_index, block_index);
}

// Forwards row-level remove intent to the controller after checking derived availability.
void EditorView::onRemovePluginPressed(std::string instance_id)
{
    if (!m_state.signal_chain.remove_plugins_enabled)
    {
        return;
    }

    m_controller.onRemovePluginRequested(std::move(instance_id));
}

// Forwards row-level move intent to the controller after checking derived availability.
void EditorView::onMovePluginPressed(
    std::string instance_id, std::size_t destination_index,
    std::vector<core::PluginBlockAssignment> placement)
{
    if (!m_state.signal_chain.move_plugins_enabled)
    {
        return;
    }

    m_controller.onMovePluginRequested(
        std::move(instance_id), destination_index, std::move(placement));
}

// Forwards the authored block placement so the controller can persist it with the project. This is
// document state, not a gated user action, so it is reported regardless of edit availability.
void EditorView::onSignalChainPlacementChanged(std::vector<core::PluginBlockAssignment> placement)
{
    m_controller.onSignalChainPlacementChanged(std::move(placement));
}

// Forwards a display-only block type override to the controller action gate.
void EditorView::onPluginDisplayTypeOverrideChanged(
    std::string instance_id, std::optional<core::PluginDisplayType> display_type)
{
    m_controller.onPluginDisplayTypeOverrideChanged(std::move(instance_id), display_type);
}

// Forwards row-level open intent to the controller; controller-side routing handles busy gating.
void EditorView::onOpenPluginPressed(std::string instance_id)
{
    m_controller.onOpenPluginRequested(std::move(instance_id));
}

// Opens input calibration through the controller when the command is available.
void EditorView::onInputCalibrationPressed()
{
    if (!m_state.signal_chain.input_calibrate_enabled)
    {
        return;
    }
    m_controller.onInputCalibrationRequested();
}

// Forwards live output gain preview changes while the slider is being dragged.
void EditorView::onOutputGainPreviewChanged(double gain_db)
{
    if (!m_state.signal_chain.output_gain_controls_enabled)
    {
        return;
    }
    m_controller.onOutputGainPreviewChanged(gain_db);
}

// Forwards committed output gain slider changes to the controller when controls are enabled.
// Routes a tone-region selection intent to the controller.
void EditorView::onToneRegionSelected(std::string region_id)
{
    m_controller.onToneRegionSelected(std::move(region_id));
}

// Routes a committed tone-region resize to the controller.
void EditorView::onToneRegionResizeRequested(
    std::string region_id, common::core::ToneGridPosition start, common::core::ToneGridPosition end)
{
    m_controller.onToneRegionResizeRequested(std::move(region_id), start, end);
}

void EditorView::onOutputGainChanged(double gain_db)
{
    if (!m_state.signal_chain.output_gain_controls_enabled)
    {
        return;
    }
    m_controller.onOutputGainChanged(gain_db);
}

// Forwards browser rescan intent to the workflow controller.
void EditorView::onPluginBrowserScanRequested()
{
    m_controller.onPluginCatalogScanRequested();
}

// Forwards the selected browser plugin to the workflow controller.
void EditorView::onPluginBrowserAddRequested(std::string plugin_id)
{
    m_controller.onSelectedPluginInsertRequested(std::move(plugin_id));
}

// Forwards browser close intent to the workflow controller.
void EditorView::onPluginBrowserClosed()
{
    m_controller.onPluginBrowserClosed();
}

// Forwards browser busy-overlay cancellation through the same controller path as the editor-wide
// overlay.
void EditorView::onPluginBrowserBusyCancelRequested()
{
    m_controller.onBusyCancelRequested();
}

} // namespace rock_hero::editor::ui
