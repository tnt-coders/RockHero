#include "editor_view.h"

#include "audio_device/audio_device_settings_window.h"
#include "audio_device/game_audio_recommendation_dialog.h"
#include "input_calibration/input_calibration_window.h"
#include "keybinds/actions_window.h"
#include "keybinds/editor_command_registry.h"
#include "keybinds/key_chord_text.h"
#include "main_window/menu_look_and_feel.h"
#include "preview/preview_window.h"
#include "shared/editor_theme.h"
#include "shared/themed_message_box.h"
#include "timeline/cursor_overlay.h"
#include "timeline/timeline_cursor.h"
#include "timeline/track_viewport.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <compare>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <memory>
#include <optional>
#include <rock_hero/common/audio/song/i_thumbnail.h>
#include <rock_hero/common/core/chart/chart_rules.h>
#include <rock_hero/common/core/package/package_id.h>
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

// Command-backed menu items use the registry's EditorCommandId values as their item ids; only
// the tablature lane-count submenu keeps raw ids. Its ids encode the requested minimum as an
// offset from this base, with the base itself meaning "match the chart" (a zero minimum). The
// range must never collide with the 0x1000+ command-id space.
constexpr int g_tab_strings_command_base{210};
constexpr int g_arrangement_selector_width{132};
// Wide enough for "Arrangement" at the default label font plus the label's side insets;
// anything narrower makes juce::Label shrink-to-fit the text below the "Grid" caption size.
constexpr int g_arrangement_caption_width{100};
constexpr int g_menu_bar_height{24};
constexpr int g_content_inset{8};
constexpr int g_control_gap{8};
// Sized for a long readout like "128.4.99 / 59:59:999" at the readout font height.
constexpr int g_position_display_width{240};
// Sized for "999 notes" at the readout font height.
constexpr int g_selection_count_display_width{110};
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

// Ensures saved tone files use the Rock Hero tone extension when needed.
[[nodiscard]] std::filesystem::path pathWithToneExtension(const juce::File& file)
{
    std::filesystem::path path = common::core::pathFromJuceFile(file);
    if (!path.empty() && path.extension().empty())
    {
        path.replace_extension(".tone");
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
// controller cannot legitimately stash in a deferred slot. When the Tone Designer's document is
// the protected work (no project open), the copy names the tone instead of project changes.
[[nodiscard]] juce::String unsavedChangesPromptMessage(
    core::EditorActionId action, bool tone_document)
{
    if (tone_document)
    {
        switch (action)
        {
            case core::EditorActionId::OpenProject:
            {
                return "Save your tone before opening a project?";
            }
            case core::EditorActionId::RestoreProject:
            {
                return "Save your tone before restoring the previous project?";
            }
            case core::EditorActionId::ImportSong:
            {
                return "Save your tone before importing a project?";
            }
            case core::EditorActionId::ExitApplication:
            {
                return "Save your tone before exiting Rock Hero Editor?";
            }
            case core::EditorActionId::NewToneDocument:
            {
                return "Save your tone before starting a new one?";
            }
            case core::EditorActionId::OpenToneFile:
            {
                return "Save your tone before opening another tone file?";
            }
            case core::EditorActionId::SaveProject:
            case core::EditorActionId::SaveProjectAs:
            case core::EditorActionId::PublishProject:
            case core::EditorActionId::CloseProject:
            case core::EditorActionId::ResolveUnsavedChangesPrompt:
            case core::EditorActionId::CancelSaveAsPrompt:
            case core::EditorActionId::CancelBusyOperation:
            case core::EditorActionId::Undo:
            case core::EditorActionId::Redo:
            case core::EditorActionId::PlayPause:
            case core::EditorActionId::Stop:
            case core::EditorActionId::SeekTimeline:
            case core::EditorActionId::SetGridNoteValue:
            case core::EditorActionId::SelectArrangement:
            case core::EditorActionId::SelectToneRegion:
            case core::EditorActionId::ResizeToneRegion:
            case core::EditorActionId::CreateToneRegion:
            case core::EditorActionId::DeleteToneRegion:
            case core::EditorActionId::RenameTone:
            case core::EditorActionId::MoveToneBoundary:
            case core::EditorActionId::CreateNewTone:
            case core::EditorActionId::ShowPluginBrowser:
            case core::EditorActionId::BeginPluginInsert:
            case core::EditorActionId::ScanPluginCatalog:
            case core::EditorActionId::InsertSelectedPlugin:
            case core::EditorActionId::RemovePlugin:
            case core::EditorActionId::MovePlugin:
            case core::EditorActionId::SetSignalChainPlacement:
            case core::EditorActionId::SetPluginDisplayTypeOverride:
            case core::EditorActionId::OpenPlugin:
            case core::EditorActionId::SetToneAutomationPoints:
            case core::EditorActionId::SaveToneFile:
            case core::EditorActionId::SaveToneFileAs:
            case core::EditorActionId::ImportToneFile:
            case core::EditorActionId::ExportToneFile:
            case core::EditorActionId::ResolveToneImportPrompt:
            {
                return "Save your tone before continuing?";
            }
        }
    }

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
        case core::EditorActionId::SelectArrangement:
        case core::EditorActionId::SelectToneRegion:
        case core::EditorActionId::ResizeToneRegion:
        case core::EditorActionId::CreateToneRegion:
        case core::EditorActionId::DeleteToneRegion:
        case core::EditorActionId::RenameTone:
        case core::EditorActionId::MoveToneBoundary:
        case core::EditorActionId::CreateNewTone:
        case core::EditorActionId::SetToneAutomationPoints:
        case core::EditorActionId::NewToneDocument:
        case core::EditorActionId::OpenToneFile:
        case core::EditorActionId::SaveToneFile:
        case core::EditorActionId::SaveToneFileAs:
        case core::EditorActionId::ImportToneFile:
        case core::EditorActionId::ExportToneFile:
        case core::EditorActionId::ResolveToneImportPrompt:
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
    , m_playback_clock(audio_ports.playback_clock)
    , m_menu_look_and_feel(std::make_unique<MenuLookAndFeel>())
    , m_menu_bar(this)
    , m_transport_controls(*this)
    , m_grid_spacing_selector(*this)
    , m_master_output_meter(AudioLevelMeterOrientation::Horizontal, "Master")
    , m_signal_chain_panel(*this)
    , m_tone_track_view(*this, m_state.tempo_map, audio_ports.transport)
    , m_tone_automation_lanes_view(*this, m_state.tempo_map, audio_ports.tone_automation)
    , m_cursor_overlay(
          std::make_unique<CursorOverlay>(controller, audio_ports.transport, m_state.tempo_map))
    , m_track_viewport(
          std::make_unique<TrackViewport>(
              controller, m_arrangement_view, m_tab_view, m_tone_track_view,
              m_tone_automation_lanes_view, *m_cursor_overlay, audio_ports.transport))
    , m_meter_vblank_attachment(this, [this] {
        refreshAudioMeters();
        refreshTimeDisplay();
    })
{
    setWantsKeyboardFocus(true);

    // Register the keybind registry: every command's info (name, category, default chords,
    // enablement) comes from this target, and the manager's key mapping set becomes the single
    // chord-to-command matcher — the owning window attaches it as a key listener. Watching the
    // manager keeps menu shortcut text current after mapping changes.
    m_command_manager.registerAllCommandsForTarget(this);
    m_command_manager.setFirstCommandTarget(this);
    setApplicationCommandManagerToWatch(&m_command_manager);

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
    // The selection-count chip (the marker model): typing acts on the whole note selection,
    // so its size stays visible beside the transport readout even when the highlighted notes
    // are scrolled off-screen. Hidden below two selected notes.
    m_selection_count_display.setComponentID("chart_selection_count_display");
    m_selection_count_display.setFont(
        juce::Font{juce::FontOptions{g_transport_readout_font_height, juce::Font::bold}});
    m_selection_count_display.setJustificationType(juce::Justification::centredLeft);
    m_selection_count_display.setInterceptsMouseClicks(false, false);
    m_selection_count_display.setVisible(false);
    m_master_output_meter.setComponentID("master_output_meter");
    m_audio_device_button.setComponentID("audio_device_button");
    m_audio_device_button.setText("Audio Device");
    m_audio_device_button.onClick = [this] { showAudioDeviceSettingsWindow(); };
    m_arrangement_view.setComponentID("arrangement_view");
    m_tab_view.setComponentID("tab_view");
    m_tab_view.setPointerEventCallback(
        [this](core::ChartPointerPhase phase, const core::ChartPointerEvent& event) {
            switch (phase)
            {
                case core::ChartPointerPhase::Down:
                {
                    m_controller.onChartPointerDown(event);
                    break;
                }
                case core::ChartPointerPhase::Drag:
                {
                    m_controller.onChartPointerDrag(event);
                    break;
                }
                case core::ChartPointerPhase::Up:
                {
                    m_controller.onChartPointerUp(event);
                    break;
                }
                case core::ChartPointerPhase::Move:
                {
                    m_controller.onChartPointerMove(event);
                    break;
                }
                case core::ChartPointerPhase::Exit:
                {
                    m_controller.onChartPointerExit();
                    break;
                }
            }
        });
    m_tone_track_view.setComponentID("tone_track_view");
    m_tone_track_view.setSnapGuideCallback([this](std::optional<TimelineSnapGuide> guide) {
        m_cursor_overlay->setSnapGuide(std::move(guide));
    });
    m_tone_automation_lanes_view.setComponentID("tone_automation_lanes_view");
    m_tone_automation_lanes_view.setSnapGuideCallback(
        [this](std::optional<TimelineSnapGuide> guide) {
            m_cursor_overlay->setSnapGuide(std::move(guide));
        });
    // Lane heights feed the viewport's height-only relayout, which skips the tempo-grid rescan.
    m_tone_automation_lanes_view.setHeightsChangedCallback([this] {
        if (m_track_viewport != nullptr)
        {
            m_track_viewport->relayoutForContentHeightChange();
        }
    });
    // Each caret-bearing lane pushes its paused-column cut-out mask straight to the viewport, so the
    // viewport never polls sibling geometry to keep the paused cursor's gap in step with the caret.
    m_tab_view.setCaretMaskCallback([this](std::optional<juce::Range<float>> mask) {
        if (m_track_viewport != nullptr)
        {
            m_track_viewport->setTabCaretMask(mask);
        }
    });
    m_tone_automation_lanes_view.setCaretMaskCallback(
        [this](std::optional<juce::Range<float>> mask) {
            if (m_track_viewport != nullptr)
            {
                m_track_viewport->setAutomationCaretMask(mask);
            }
        });
    m_cursor_overlay->setHitTestPassThrough([this](juce::Point<int> position) {
        // The tab lane claims its whole band while a chart is displayed; its controller-side
        // gesture policy still turns empty clicks into seeks, so seeking is preserved.
        const juce::Rectangle<int> tab_bounds = m_tab_view.getBounds();
        if (tab_bounds.contains(position) &&
            m_tab_view.wantsPointerAt(position - tab_bounds.getPosition()))
        {
            return true;
        }
        const juce::Rectangle<int> row_bounds = m_tone_track_view.getBounds();
        if (row_bounds.contains(position) &&
            m_tone_track_view.wantsPointerAt(position - row_bounds.getPosition()))
        {
            return true;
        }
        const juce::Rectangle<int> lanes_bounds = m_tone_automation_lanes_view.getBounds();
        return lanes_bounds.contains(position) &&
               m_tone_automation_lanes_view.wantsPointerAt(position - lanes_bounds.getPosition());
    });
    m_busy_overlay.setComponentID("busy_overlay");
    m_busy_overlay.setPaintCallback([this] { handleBusyOverlayPainted(); });
    m_busy_overlay.setCancelCallback([this] { m_controller.onBusyCancelRequested(); });

    m_audio_device_failure_overlay.setComponentID("audio_device_failure_overlay");
    m_audio_device_failure_overlay.setRetryCallback([this] {
        m_controller.onAudioDeviceFailureDecision(core::AudioDeviceFailureDecision::Retry);
    });
    m_audio_device_failure_overlay.setOpenSettingsCallback([this] {
        m_controller.onAudioDeviceFailureDecision(core::AudioDeviceFailureDecision::OpenSettings);
        showAudioDeviceSettingsWindow();
    });

    m_arrangement_view.setThumbnailFactory(audio_ports.thumbnail_factory);

    addAndMakeVisible(m_menu_bar);
    addAndMakeVisible(m_transport_controls);
    addAndMakeVisible(m_position_display);
    addChildComponent(m_selection_count_display);
    addAndMakeVisible(m_grid_spacing_selector);
    m_arrangement_caption.setComponentID("arrangement_caption");
    m_arrangement_caption.setText("Arrangement", juce::dontSendNotification);
    m_arrangement_caption.setJustificationType(juce::Justification::centredRight);
    m_arrangement_caption.setInterceptsMouseClicks(false, false);
    addAndMakeVisible(m_arrangement_caption);
    m_arrangement_selector.setComponentID("arrangement_selector");
    m_arrangement_selector.onChange = [this] {
        const int item_index = m_arrangement_selector.getSelectedItemIndex();
        const auto& choices = m_state.arrangement.choices;
        if (item_index >= 0 && static_cast<std::size_t>(item_index) < choices.size() &&
            !choices[static_cast<std::size_t>(item_index)].selected)
        {
            m_controller.onArrangementSelected(choices[static_cast<std::size_t>(item_index)].id);
        }
    };
    addAndMakeVisible(m_arrangement_selector);
    addAndMakeVisible(m_master_output_meter);
    addAndMakeVisible(m_audio_device_button);
    addAndMakeVisible(m_signal_chain_panel);
    addAndMakeVisible(*m_track_viewport);
    // BusyOverlay is added last so it lands on top of the editor child stack. It also calls
    // toFront() on activation, but adding it here as the final child means the initial Z-order
    // is already correct before the first push.
    // The history inspector floats above the track stack but below the busy overlay; it starts
    // hidden and the user reveals it on demand.
    addChildComponent(m_undo_history_overlay);
    // The failure overlay sits above the editor content; the busy overlay is added after it so a
    // Retry reopen paints its busy presentation on top.
    addChildComponent(m_audio_device_failure_overlay);
    addChildComponent(m_busy_overlay);
    m_track_viewport->setProjectLoaded(m_state.project_loaded);
    // Zoom is app-local resume state like the cursor; the controller persists it per project.
    m_track_viewport->setZoomChangedCallback([this](double pixels_per_second) {
        m_controller.onTimelineZoomChanged(pixels_per_second);
    });
    // The timeline content consumes wheels for zoom; Alt-modified ones route here so the
    // selection verbs work over the whole timeline, not just the tab lane.
    m_track_viewport->setSelectionWheelCallback(
        [this](const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel) {
            return dispatchSelectionWheel(event, wheel);
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
    m_audio_device_failure_overlay.setRetryCallback({});
    m_audio_device_failure_overlay.setOpenSettingsCallback({});
    m_menu_bar.setLookAndFeel(nullptr);
    m_menu_bar.setModel(nullptr);
}

// Projects controller-derived state into child widgets and cursor mapping state.
// Sets the top-level window title to "<project> - <app>" when a project is open, or the bare app
// name otherwise, so the open project reads at a glance like REAPER's title bar.
void EditorView::updateWindowTitle()
{
    juce::Component* const top_level = getTopLevelComponent();
    if (top_level == nullptr)
    {
        return;
    }

    juce::String title{"Rock Hero Editor"};
    if (juce::JUCEApplicationBase* const app = juce::JUCEApplicationBase::getInstance())
    {
        title = app->getApplicationName();
    }

    if (m_state.project_file.has_value())
    {
        const juce::String project_name =
            common::core::juceFileFromPath(*m_state.project_file).getFileNameWithoutExtension();
        if (project_name.isNotEmpty())
        {
            title = project_name + " - " + title;
        }
    }

    top_level->setName(title);
}

void EditorView::setState(const core::EditorViewState& state)
{
    const core::EditorViewState previous_state = m_state;
    m_state = state;
    if (!m_state.busy.has_value())
    {
        m_after_busy_overlay_paint = {};
    }

    // Feed the history inspector every push so it tracks the stack live (this is also how the
    // storm of idle plugin-state edits becomes visible as it grows).
    m_undo_history_overlay.setHistory(m_state.undo_history);

    // Live-edit updates for the 3D preview: pointer identity stands in for content equality on
    // the shared snapshot, so ordinary pushes never rebuild the retained board geometry.
    if (m_preview_window != nullptr && previous_state.highway != m_state.highway)
    {
        m_preview_window->setHighwayState(m_state.highway);
    }

    // The paused preview shows the marker-rule time (the armed caret is THE paused position),
    // so caret moves must land in the 3D view immediately.
    if (m_preview_window != nullptr && previous_state.chart_edit.caret != m_state.chart_edit.caret)
    {
        m_preview_window->setCaretSeconds(
            m_state.chart_edit.caret.has_value()
                ? std::optional<double>{m_state.chart_edit.caret->seconds}
                : std::nullopt);
    }

    if (previous_state.project_file != m_state.project_file)
    {
        updateWindowTitle();
    }

    menuItemsChanged();
    // Command enablement derives from the same state push; refreshing here keeps the mapping
    // set's availability checks and any command-driven UI in step with the menus.
    m_command_manager.commandStatusChanged();
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
    if (previous_state.arrangement.choices != m_state.arrangement.choices)
    {
        m_arrangement_selector.clear(juce::dontSendNotification);
        for (std::size_t index = 0; index < m_state.arrangement.choices.size(); ++index)
        {
            m_arrangement_selector.addItem(
                juce::String{m_state.arrangement.choices[index].label},
                static_cast<int>(index) + 1);
            if (m_state.arrangement.choices[index].selected)
            {
                m_arrangement_selector.setSelectedItemIndex(
                    static_cast<int>(index), juce::dontSendNotification);
            }
        }
    }
    m_arrangement_selector.setEnabled(
        m_state.project_loaded && m_state.arrangement.choices.size() > 1);
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
    m_arrangement_view.setWaveformVisible(m_state.waveform_visible);

    m_tab_view.setVisibleTimeline(m_state.visible_timeline);
    m_tab_view.setState(m_state.tab, m_state.tab_minimum_displayed_strings);
    m_tab_view.setEditState(m_state.chart_edit);
    // The viewport needs the displayed lane count because counts past the six-string reference
    // density grow the waveform row instead of compressing the tablature lanes.
    m_track_viewport->setTabDisplayedStrings(tabDisplayedStringCount(
        m_state.tab != nullptr ? m_state.tab->string_count : 0,
        m_state.tab_minimum_displayed_strings));
    // An armed caret hides the paused playhead (the caret is the position display) and
    // becomes the wheel-zoom center; passive keeps the paused cursor line at the transport
    // position and zooms around it. A lane-riding caret (§9b) is armed all the same, just
    // published through the automation state instead of the chart overlay. Ordering against the
    // caret-bearing views' setState no longer matters: the paused column's caret mask is pushed
    // by those views (setTab/AutomationCaretMask), not polled here.
    m_track_viewport->setArmedChartCaret(
        m_state.chart_edit.caret.has_value()
            ? std::optional<double>{m_state.chart_edit.caret->seconds}
        : m_state.tone_automation.lane_caret.has_value()
            ? std::optional<double>{m_state.tone_automation.lane_caret->seconds}
            : std::nullopt);
    // Caret navigation keeps its measure comfortably in view: whenever the caret lands at a
    // new time (arming or stepping — string-only moves keep the same seconds and glide
    // nothing), the window glides until the caret's measure is fully visible, with the same
    // eased shift playback follow uses. One reveal rule serves both caret rows (the two can
    // never both fire — the caret publishes through exactly one of the two states).
    const auto glide_if_caret_moved =
        [this](const auto& previous_caret, const auto& current_caret) {
            if (current_caret.has_value() &&
                (!previous_caret.has_value() ||
                 std::is_neq(previous_caret->seconds <=> current_caret->seconds)))
            {
                m_track_viewport->ensureMeasureVisible(
                    current_caret->measure_start_seconds,
                    current_caret->measure_end_seconds,
                    current_caret->seconds);
            }
        };
    glide_if_caret_moved(previous_state.chart_edit.caret, m_state.chart_edit.caret);
    glide_if_caret_moved(
        previous_state.tone_automation.lane_caret, m_state.tone_automation.lane_caret);
    // The count chip appears from two selected notes up: typing acts on the whole selection,
    // so its size must stay visible even with the highlights scrolled off-screen.
    const std::size_t selected_count = m_state.chart_edit.selected_notes.size();
    m_selection_count_display.setVisible(selected_count >= 2);
    if (selected_count >= 2)
    {
        m_selection_count_display.setText(
            juce::String{static_cast<int>(selected_count)} + " notes", juce::dontSendNotification);
    }

    // The ruler's bottom band shows the tab projection's named chord/arpeggio spans directly
    // above the lane's rails; unnamed shapes get no chip by design.
    std::vector<RulerShapeLabel> shape_labels;
    if (m_state.tab != nullptr)
    {
        for (const common::core::TabShapeView& shape : m_state.tab->shapes)
        {
            if (!shape.name.empty())
            {
                shape_labels.push_back(
                    RulerShapeLabel{
                        .seconds = shape.start_seconds,
                        .name = juce::String{shape.name},
                        .arpeggio = shape.arpeggio,
                    });
            }
        }
    }
    m_track_viewport->setShapeLabels(std::move(shape_labels));

    // The ruler's section chip row shows the song's section markers as a pinned marker lane.
    std::vector<RulerSectionLabel> section_labels;
    section_labels.reserve(m_state.sections.size());
    for (const core::SongSectionView& section : m_state.sections)
    {
        section_labels.push_back(
            RulerSectionLabel{
                .seconds = section.seconds,
                .name = juce::String{section.name},
            });
    }
    m_track_viewport->setSectionLabels(std::move(section_labels));

    m_tone_track_view.setVisibleTimeline(m_state.visible_timeline);
    m_tone_track_view.setGridNoteValue(m_state.grid_note_value);
    m_tone_track_view.setState(m_state.tone_track);

    m_tone_automation_lanes_view.setVisibleTimeline(m_state.visible_timeline);
    m_tone_automation_lanes_view.setGridNoteValue(m_state.grid_note_value);
    // One scan resolves the active region for both consumers below: the lanes' editable
    // window (the lane is authored per tone but edited per region instance — the active
    // region defines the span) and the signal-chain header's tone name (the panel edits the
    // active == audible tone).
    const core::ToneRegionViewState* active_region = nullptr;
    for (const core::ToneRegionViewState& region : m_state.tone_track.regions)
    {
        if (region.active)
        {
            active_region = &region;
            break;
        }
    }
    m_tone_automation_lanes_view.setEditableWindow(
        active_region != nullptr ? active_region->time_range : common::core::TimeRange{});
    m_tone_automation_lanes_view.setState(m_state.tone_automation);

    m_signal_chain_panel.setToneName(
        active_region != nullptr ? active_region->name : std::string{});
    // While the designer is active the panel header shows the tone document instead of a
    // project catalog tone; the slice also carries the file-strip visibility.
    m_signal_chain_panel.setToneDesignerState(m_state.tone_designer);

    m_cursor_overlay->setVisibleTimelineRange(m_state.visible_timeline);
    m_cursor_overlay->setGridNoteValue(m_state.grid_note_value);
    m_cursor_overlay->setTimeSelectionRange(m_state.time_selection);
    presentUnsavedChangesPromptIfNeeded(m_state.unsaved_changes_prompt);
    presentSaveAsPromptIfNeeded(m_state.save_as_prompt);
    presentToneImportPromptIfNeeded(m_state.tone_import_prompt);
    presentRestoreInterruptedPromptIfNeeded(m_state.restore_interrupted_prompt);
    presentGameAudioUnavailablePromptIfNeeded(m_state.game_audio_unavailable_prompt);
    presentGameAudioRecommendationIfNeeded(m_state.game_audio_recommendation_prompt);
    presentInputCalibrationPromptIfNeeded(m_state.input_calibration_prompt);
    presentPluginBrowserIfNeeded(m_state.plugin_browser);
    m_audio_device_failure_overlay.setPrompt(m_state.audio_device_failure_prompt);
    m_busy_overlay.setBusyState(m_state.busy);
    repaint();
}

// Presents controller-reported workflow failures as one-shot dialogs.
void EditorView::showError(const std::string& message)
{
    showThemedWarningBox(
        this, "Could not complete request", juce::String::fromUTF8(message.c_str()));
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
    g.fillAll(editorTheme().window_background);

    g.setColour(editorTheme().bar_background);
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
    m_arrangement_caption.setBounds(
        control_row.removeFromLeft(std::min(g_arrangement_caption_width, control_row.getWidth())));
    // The combo fills the strip height with the same 4px inner gap as the grid selector's box so
    // the two selectors read as one control family.
    m_arrangement_selector.setBounds(
        control_row.removeFromLeft(std::min(g_arrangement_selector_width, control_row.getWidth()))
            .reduced(4, 0));
    control_row.removeFromLeft(g_control_gap);
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
    m_selection_count_display.setBounds(
        playback_area
            .removeFromLeft(std::min(g_selection_count_display_width, playback_area.getWidth()))
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
    m_audio_device_failure_overlay.setBounds(getLocalBounds());
    m_busy_overlay.setBounds(getLocalBounds());

    // Pin the history inspector to the top-right, below the transport strip, tall enough to list a
    // useful window of entries without covering the whole canvas.
    constexpr int panel_width = 340;
    const int panel_top = g_menu_bar_height + g_transport_bar_height + 8;
    const int panel_height = std::clamp(getHeight() - panel_top - 12, 80, 460);
    m_undo_history_overlay.setBounds(
        std::max(0, getWidth() - panel_width - 12), panel_top, panel_width, panel_height);
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

// Shows or hides the undo-history inspector, bringing it to the front when revealed.
void EditorView::toggleUndoHistoryPanel()
{
    const bool show = !m_undo_history_overlay.isVisible();
    m_undo_history_overlay.setVisible(show);
    if (show)
    {
        m_undo_history_overlay.toFront(false);
    }
}

// Selection verbs follow the selection, not the pointer (user decision 2026-07-17): with a
// chart selection active, Alt+wheel (sustain — Ctrl composes the 1/960 fine step) and
// Alt+Shift+wheel (fret shift) act on it wherever the pointer sits inside the editor window.
bool EditorView::dispatchSelectionWheel(
    const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel)
{
    const bool chart_shown = m_state.tab != nullptr && m_state.tab->string_count > 0;
    if (!event.mods.isAltDown() || std::is_eq(wheel.deltaY <=> 0.0f) || !chart_shown ||
        m_state.chart_edit.selected_notes.empty())
    {
        return false;
    }

    const int direction = wheel.deltaY > 0.0f ? 1 : -1;
    if (event.mods.isShiftDown())
    {
        m_controller.onChartFretShiftRequested(direction);
    }
    else
    {
        m_controller.onChartSustainAdjustRequested(direction, event.mods.isCtrlDown());
    }
    return true;
}

// Catches Alt-modified wheels bubbling from surfaces that do not consume wheels themselves
// (the ruler, the transport strip, empty chrome); the timeline content routes its own through
// the TrackViewport callback since zoom would otherwise swallow them.
void EditorView::mouseWheelMove(const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel)
{
    if (dispatchSelectionWheel(event, wheel))
    {
        return;
    }
    juce::Component::mouseWheelMove(event, wheel);
}

// Creates the preview window on first use, then shows or hides it; hiding suspends the render
// surface's frame ticks while its GPU stack stays alive (bgfx cannot re-initialize in-process).
void EditorView::togglePreviewWindow()
{
    if (m_preview_window == nullptr)
    {
        m_preview_window = std::make_unique<PreviewWindow>(
            m_transport,
            m_playback_clock,
            [this](const juce::KeyPress& key) {
                // Transport keys, the preview toggle, and the forward/backward song-navigation
                // verbs (44-Q4, widened 2026-07-22: the paused preview follows the marker, so
                // caret time travel is meaningful from this window). Editing shortcuts (delete,
                // nudge, undo) stay with the main window, which shows the selection context.
                // Resolved through the command mappings, not hardcoded chords, so future
                // rebinds of rebindable commands stay honored.
                static constexpr std::array g_preview_commands{
                    EditorCommandId::PlayPause,
                    EditorCommandId::TogglePreview3D,
                    EditorCommandId::CaretStepLeft,
                    EditorCommandId::CaretStepRight,
                    EditorCommandId::CaretMeasureJumpLeft,
                    EditorCommandId::CaretMeasureJumpRight,
                    EditorCommandId::CaretJumpChartStart,
                    EditorCommandId::CaretJumpChartEnd,
                    EditorCommandId::CaretJumpPreviousSection,
                    EditorCommandId::CaretJumpNextSection,
                };
                const juce::CommandID command =
                    m_command_manager.getKeyMappings()->findCommandForKeyPress(key);
                if (!std::ranges::any_of(g_preview_commands, [command](const EditorCommandId id) {
                        return command == toJuceCommandId(id);
                    }))
                {
                    return false;
                }
                juce::ApplicationCommandInfo info{command};
                getCommandInfo(command, info);
                if ((info.flags & juce::ApplicationCommandInfo::isDisabled) != 0)
                {
                    return false;
                }
                return m_command_manager.invokeDirectly(command, false);
            },
            getTopLevelComponent());
    }

    if (m_preview_window->isVisible())
    {
        m_preview_window->close();
    }
    else
    {
        m_preview_window->setHighwayState(m_state.highway);
        m_preview_window->setCaretSeconds(
            m_state.chart_edit.caret.has_value()
                ? std::optional<double>{m_state.chart_edit.caret->seconds}
                : std::nullopt);
        m_preview_window->open();
    }
}

// Creates the keyboard-shortcuts window on first use, then shows it; the window survives closes
// so its tree state and any in-progress inspection are kept across reopenings.
bool EditorView::chartShown() const noexcept
{
    return m_state.tab != nullptr && m_state.tab->string_count > 0;
}

void EditorView::showActionsWindow()
{
    if (m_actions_window == nullptr)
    {
        m_actions_window =
            std::make_unique<ActionsWindow>(m_command_manager, getTopLevelComponent());
    }
    m_actions_window->open();
}

// Returns the top-level editor menus displayed by the editor.
juce::StringArray EditorView::getMenuBarNames()
{
    return {"File", "Edit", "View"};
}

// Builds menus using only controller-derived state.
juce::PopupMenu EditorView::getMenuForIndex(int top_level_menu_index, const juce::String& menu_name)
{
    // Command-backed items display their live shortcut, so the text stays correct after any
    // rebind. Name, enablement, and tick state all come from getCommandInfo — one source for
    // menus and keys — and addEditorCommandItem renders the shortcut through the shared
    // keyChordText formatter so menus can never drift from the actions dialog's chips.
    if (top_level_menu_index == 0 && menu_name == "File")
    {
        juce::PopupMenu menu;
        addEditorCommandItem(menu, m_command_manager, EditorCommandId::OpenProject);
        addEditorCommandItem(menu, m_command_manager, EditorCommandId::ImportSong);
        menu.addSeparator();
        addEditorCommandItem(menu, m_command_manager, EditorCommandId::SaveProject);
        addEditorCommandItem(menu, m_command_manager, EditorCommandId::SaveProjectAs);
        addEditorCommandItem(menu, m_command_manager, EditorCommandId::PublishSong);
        menu.addSeparator();
        addEditorCommandItem(menu, m_command_manager, EditorCommandId::CloseProject);
        addEditorCommandItem(menu, m_command_manager, EditorCommandId::ExitEditor);
        return menu;
    }

    if (top_level_menu_index == 1 && menu_name == "Edit")
    {
        juce::PopupMenu menu;
        addEditorCommandItem(menu, m_command_manager, EditorCommandId::Undo);
        addEditorCommandItem(menu, m_command_manager, EditorCommandId::Redo);
        menu.addSeparator();
        addEditorCommandItem(menu, m_command_manager, EditorCommandId::ShowActions);
        return menu;
    }

    if (top_level_menu_index == 2 && menu_name == "View")
    {
        juce::PopupMenu menu;
        addEditorCommandItem(menu, m_command_manager, EditorCommandId::ToggleWaveform);
        addEditorCommandItem(menu, m_command_manager, EditorCommandId::ToggleUndoHistory);
        addEditorCommandItem(menu, m_command_manager, EditorCommandId::TogglePreview3D);

        // The lane-count submenu offers "match the chart" plus explicit minimums up to the
        // format's string cap; picking fewer lanes than the chart has can never hide notes
        // because the chart's own count floors the displayed count.
        const bool has_chart = m_state.tab != nullptr && m_state.tab->string_count > 0;
        juce::PopupMenu strings_menu;
        strings_menu.addItem(
            g_tab_strings_command_base,
            "Match Chart",
            has_chart,
            m_state.tab_minimum_displayed_strings == 0);
        for (int count = 4; count <= common::core::g_max_chart_strings; ++count)
        {
            strings_menu.addItem(
                g_tab_strings_command_base + count,
                juce::String{count} + " Strings",
                has_chart,
                m_state.tab_minimum_displayed_strings == count);
        }
        menu.addSubMenu("Tablature Strings", strings_menu, has_chart);
        return menu;
    }

    return {};
}

// Routes raw-id menu selections. Command-backed items dispatch through the command manager
// (their selection lands in perform), so only the tablature lane-count submenu routes here.
void EditorView::menuItemSelected(int menu_item_id, int /*top_level_menu_index*/)
{
    // Lane-count submenu ids encode the requested minimum as an offset from the base; the base
    // itself requests a zero minimum (match the chart).
    if (menu_item_id >= g_tab_strings_command_base &&
        menu_item_id <= g_tab_strings_command_base + common::core::g_max_chart_strings)
    {
        m_controller.onTabMinimumDisplayedStringsChangeRequested(
            menu_item_id - g_tab_strings_command_base);
    }
}

juce::ApplicationCommandManager& EditorView::commandManager() noexcept
{
    return m_command_manager;
}

juce::ApplicationCommandTarget* EditorView::getNextCommandTarget()
{
    return nullptr;
}

void EditorView::getAllCommands(juce::Array<juce::CommandID>& commands)
{
    for (const EditorCommandSpec& spec : editorCommandRegistry())
    {
        commands.add(toJuceCommandId(spec.id));
    }
}

// One source for menus and key dispatch: names, categories, and default chords come from the
// registry table; enablement and tick state derive from the same pushed view state the menus
// used before the registry existed, so behavior is preserved exactly.
void EditorView::getCommandInfo(juce::CommandID command_id, juce::ApplicationCommandInfo& info)
{
    const EditorCommandSpec* const spec = findEditorCommandSpec(command_id);
    if (spec == nullptr)
    {
        return;
    }

    info.setInfo(spec->name, spec->name, spec->category, 0);
    for (const juce::KeyPress& key : spec->default_keypresses)
    {
        info.defaultKeypresses.add(key);
    }
    switch (static_cast<EditorCommandId>(command_id))
    {
        case EditorCommandId::OpenProject:
        {
            info.setActive(m_state.open_enabled);
            break;
        }
        case EditorCommandId::ImportSong:
        {
            info.setActive(m_state.import_enabled);
            break;
        }
        case EditorCommandId::SaveProject:
        {
            info.setActive(m_state.save_enabled);
            break;
        }
        case EditorCommandId::SaveProjectAs:
        {
            info.setActive(m_state.save_as_enabled);
            break;
        }
        case EditorCommandId::PublishSong:
        {
            info.setActive(m_state.publish_enabled);
            break;
        }
        case EditorCommandId::CloseProject:
        {
            info.setActive(m_state.close_enabled);
            break;
        }
        case EditorCommandId::ExitEditor:
        {
            break;
        }
        case EditorCommandId::Undo:
        {
            info.shortName = editCommandText("Undo", m_state.undo_label);
            info.setActive(m_state.undo_enabled);
            break;
        }
        case EditorCommandId::Redo:
        {
            info.shortName = editCommandText("Redo", m_state.redo_label);
            info.setActive(m_state.redo_enabled);
            break;
        }
        case EditorCommandId::ShowActions:
        {
            break;
        }
        case EditorCommandId::PlayPause:
        {
            // Always active: the controller owns transport legality, matching the old
            // unconditional Space handling.
            break;
        }
        case EditorCommandId::ToggleWaveform:
        {
            info.setActive(m_state.project_loaded);
            info.setTicked(m_state.waveform_visible);
            break;
        }
        case EditorCommandId::ToggleUndoHistory:
        {
            info.setTicked(m_undo_history_overlay.isVisible());
            break;
        }
        case EditorCommandId::TogglePreview3D:
        {
            // Opening needs a project; closing must stay available after a project close, or a
            // close would strand an open preview behind a dead toggle.
            const bool preview_open = m_preview_window != nullptr && m_preview_window->isVisible();
            info.setActive(m_state.project_loaded || preview_open);
            info.setTicked(preview_open);
            break;
        }
        case EditorCommandId::InsertToneChange:
        {
            break;
        }

        // The grammar verbs (plan 53 Phase 1b) stay always-active on purpose: their old
        // decoder branches declined silently, and a disabled command whose chord matches makes
        // JUCE play the system alert sound (KeyPressMappingSet::keyPressed) — so perform
        // self-gates instead, and the core self-gates its intents anyway.
        case EditorCommandId::CaretStepLeft:
        case EditorCommandId::CaretStepRight:
        case EditorCommandId::CaretStepUp:
        case EditorCommandId::CaretStepDown:
        case EditorCommandId::CaretMeasureJumpLeft:
        case EditorCommandId::CaretMeasureJumpRight:
        case EditorCommandId::CaretJumpChartStart:
        case EditorCommandId::CaretJumpChartEnd:
        case EditorCommandId::CaretJumpPreviousSection:
        case EditorCommandId::CaretJumpNextSection:
        case EditorCommandId::TimeSelectionExtendLeft:
        case EditorCommandId::TimeSelectionExtendRight:
        case EditorCommandId::TimeSelectionExtendMeasureLeft:
        case EditorCommandId::TimeSelectionExtendMeasureRight:
        case EditorCommandId::TimeSelectionExtendPreviousSection:
        case EditorCommandId::TimeSelectionExtendNextSection:
        case EditorCommandId::TimeSelectionExtendChartStart:
        case EditorCommandId::TimeSelectionExtendChartEnd:
        case EditorCommandId::SelectionMoveLeft:
        case EditorCommandId::SelectionMoveRight:
        case EditorCommandId::SelectionMoveUp:
        case EditorCommandId::SelectionMoveDown:
        case EditorCommandId::SelectionMoveFineLeft:
        case EditorCommandId::SelectionMoveFineRight:
        case EditorCommandId::SelectionMoveFineUp:
        case EditorCommandId::SelectionMoveFineDown:
        case EditorCommandId::SelectionDelete:
        case EditorCommandId::SustainLengthen:
        case EditorCommandId::SustainShorten:
        case EditorCommandId::SustainLengthenFine:
        case EditorCommandId::SustainShortenFine:
        case EditorCommandId::FretShiftUp:
        case EditorCommandId::FretShiftDown:
        case EditorCommandId::NeutralInsert:
        case EditorCommandId::CancelDismiss:
        case EditorCommandId::TypeDigit0:
        case EditorCommandId::TypeDigit1:
        case EditorCommandId::TypeDigit2:
        case EditorCommandId::TypeDigit3:
        case EditorCommandId::TypeDigit4:
        case EditorCommandId::TypeDigit5:
        case EditorCommandId::TypeDigit6:
        case EditorCommandId::TypeDigit7:
        case EditorCommandId::TypeDigit8:
        case EditorCommandId::TypeDigit9:
        case EditorCommandId::GridFiner:
        case EditorCommandId::GridCoarser:
        case EditorCommandId::ZoomIn:
        case EditorCommandId::ZoomOut:
        {
            break;
        }
    }
}

// The guards mirror getCommandInfo's enablement on purpose: the mapping set and menus already
// gate on it, but direct invocation paths (tests, the preview-window filter) must stay safe too.
bool EditorView::perform(const InvocationInfo& info)
{
    switch (static_cast<EditorCommandId>(info.commandID))
    {
        case EditorCommandId::OpenProject:
        {
            if (m_state.open_enabled)
            {
                showOpenChooser();
            }
            return true;
        }
        case EditorCommandId::ImportSong:
        {
            if (m_state.import_enabled)
            {
                showImportChooser();
            }
            return true;
        }
        case EditorCommandId::SaveProject:
        {
            if (!m_state.save_enabled)
            {
                return true;
            }
            if (m_state.save_requires_destination)
            {
                showSaveAsChooser(SaveAsChooserPurpose::UserSaveAs);
            }
            else
            {
                m_controller.onSaveRequested();
            }
            return true;
        }
        case EditorCommandId::SaveProjectAs:
        {
            if (m_state.save_as_enabled)
            {
                showSaveAsChooser(SaveAsChooserPurpose::UserSaveAs);
            }
            return true;
        }
        case EditorCommandId::PublishSong:
        {
            if (m_state.publish_enabled)
            {
                showPublishChooser();
            }
            return true;
        }
        case EditorCommandId::CloseProject:
        {
            if (m_state.close_enabled)
            {
                m_controller.onCloseRequested();
            }
            return true;
        }
        case EditorCommandId::ExitEditor:
        {
            m_controller.onExitRequested();
            return true;
        }
        case EditorCommandId::Undo:
        {
            if (m_state.undo_enabled)
            {
                m_controller.onUndoRequested();
            }
            return true;
        }
        case EditorCommandId::Redo:
        {
            if (m_state.redo_enabled)
            {
                m_controller.onRedoRequested();
            }
            return true;
        }
        case EditorCommandId::ShowActions:
        {
            showActionsWindow();
            return true;
        }
        case EditorCommandId::PlayPause:
        {
            m_controller.onPlayPausePressed();
            return true;
        }
        case EditorCommandId::ToggleWaveform:
        {
            if (m_state.project_loaded)
            {
                m_controller.onWaveformVisibleChangeRequested(!m_state.waveform_visible);
            }
            return true;
        }
        case EditorCommandId::ToggleUndoHistory:
        {
            toggleUndoHistoryPanel();
            return true;
        }
        case EditorCommandId::TogglePreview3D:
        {
            if (m_state.project_loaded ||
                (m_preview_window != nullptr && m_preview_window->isVisible()))
            {
                togglePreviewWindow();
            }
            return true;
        }
        case EditorCommandId::InsertToneChange:
        {
            createToneMarkerAtCursor();
            return true;
        }

            // ---- Grammar verbs (plan 53 Phase 1b). Each perform is the old decoder branch moved
            // verbatim onto the same controller intents, so undo and gesture semantics are
            // untouched — only the trigger moved into the mapping set. Guards mirror the decoder's
            // silent declines; see getCommandInfo for why these register always-active.

        case EditorCommandId::CaretStepLeft:
        {
            if (chartShown())
            {
                m_controller.onChartCaretStepRequested(core::ChartStepDirection::Left, false);
            }
            return true;
        }
        case EditorCommandId::CaretStepRight:
        {
            if (chartShown())
            {
                m_controller.onChartCaretStepRequested(core::ChartStepDirection::Right, false);
            }
            return true;
        }
        case EditorCommandId::CaretStepUp:
        {
            if (chartShown())
            {
                m_controller.onChartCaretStepRequested(core::ChartStepDirection::Up, false);
            }
            return true;
        }
        case EditorCommandId::CaretStepDown:
        {
            if (chartShown())
            {
                m_controller.onChartCaretStepRequested(core::ChartStepDirection::Down, false);
            }
            return true;
        }
        case EditorCommandId::CaretMeasureJumpLeft:
        {
            if (chartShown())
            {
                m_controller.onChartCaretStepRequested(core::ChartStepDirection::Left, true);
            }
            return true;
        }
        case EditorCommandId::CaretMeasureJumpRight:
        {
            if (chartShown())
            {
                m_controller.onChartCaretStepRequested(core::ChartStepDirection::Right, true);
            }
            return true;
        }

        // The controller resolves each jump destination and no-ops without a chart or while
        // playing, so these forward directly.
        case EditorCommandId::CaretJumpChartStart:
        {
            m_controller.onChartCaretJumpRequested(core::ChartCaretJump::ChartStart);
            return true;
        }
        case EditorCommandId::CaretJumpChartEnd:
        {
            m_controller.onChartCaretJumpRequested(core::ChartCaretJump::ChartEnd);
            return true;
        }
        case EditorCommandId::CaretJumpPreviousSection:
        {
            m_controller.onChartCaretJumpRequested(core::ChartCaretJump::PreviousSection);
            return true;
        }
        case EditorCommandId::CaretJumpNextSection:
        {
            m_controller.onChartCaretJumpRequested(core::ChartCaretJump::NextSection);
            return true;
        }

        case EditorCommandId::TimeSelectionExtendLeft:
        {
            m_controller.onTimeSelectionExtendRequested(
                core::TimeSelectionExtent::Grid, core::ChartStepDirection::Left);
            return true;
        }
        case EditorCommandId::TimeSelectionExtendRight:
        {
            m_controller.onTimeSelectionExtendRequested(
                core::TimeSelectionExtent::Grid, core::ChartStepDirection::Right);
            return true;
        }
        case EditorCommandId::TimeSelectionExtendMeasureLeft:
        {
            m_controller.onTimeSelectionExtendRequested(
                core::TimeSelectionExtent::Measure, core::ChartStepDirection::Left);
            return true;
        }
        case EditorCommandId::TimeSelectionExtendMeasureRight:
        {
            m_controller.onTimeSelectionExtendRequested(
                core::TimeSelectionExtent::Measure, core::ChartStepDirection::Right);
            return true;
        }
        case EditorCommandId::TimeSelectionExtendPreviousSection:
        {
            m_controller.onTimeSelectionExtendRequested(
                core::TimeSelectionExtent::Section, core::ChartStepDirection::Left);
            return true;
        }
        case EditorCommandId::TimeSelectionExtendNextSection:
        {
            m_controller.onTimeSelectionExtendRequested(
                core::TimeSelectionExtent::Section, core::ChartStepDirection::Right);
            return true;
        }
        case EditorCommandId::TimeSelectionExtendChartStart:
        {
            m_controller.onTimeSelectionExtendRequested(
                core::TimeSelectionExtent::ChartBound, core::ChartStepDirection::Left);
            return true;
        }
        case EditorCommandId::TimeSelectionExtendChartEnd:
        {
            m_controller.onTimeSelectionExtendRequested(
                core::TimeSelectionExtent::ChartBound, core::ChartStepDirection::Right);
            return true;
        }

        // The one selection-move intent: the controller dispatches on the selection's kind and
        // falls back to create-then-nudge at an armed empty lane slot.
        case EditorCommandId::SelectionMoveLeft:
        {
            m_controller.onSelectionMoveRequested(core::ChartStepDirection::Left, false);
            return true;
        }
        case EditorCommandId::SelectionMoveRight:
        {
            m_controller.onSelectionMoveRequested(core::ChartStepDirection::Right, false);
            return true;
        }
        case EditorCommandId::SelectionMoveUp:
        {
            m_controller.onSelectionMoveRequested(core::ChartStepDirection::Up, false);
            return true;
        }
        case EditorCommandId::SelectionMoveDown:
        {
            m_controller.onSelectionMoveRequested(core::ChartStepDirection::Down, false);
            return true;
        }
        case EditorCommandId::SelectionMoveFineLeft:
        {
            m_controller.onSelectionMoveRequested(core::ChartStepDirection::Left, true);
            return true;
        }
        case EditorCommandId::SelectionMoveFineRight:
        {
            m_controller.onSelectionMoveRequested(core::ChartStepDirection::Right, true);
            return true;
        }
        case EditorCommandId::SelectionMoveFineUp:
        {
            m_controller.onSelectionMoveRequested(core::ChartStepDirection::Up, true);
            return true;
        }
        case EditorCommandId::SelectionMoveFineDown:
        {
            m_controller.onSelectionMoveRequested(core::ChartStepDirection::Down, true);
            return true;
        }

        case EditorCommandId::SelectionDelete:
        {
            if (m_state.selection_present)
            {
                m_controller.onSelectionDeleteRequested();
            }
            return true;
        }

        case EditorCommandId::SustainLengthen:
        {
            m_controller.onChartSustainAdjustRequested(1, false);
            return true;
        }
        case EditorCommandId::SustainShorten:
        {
            m_controller.onChartSustainAdjustRequested(-1, false);
            return true;
        }
        case EditorCommandId::SustainLengthenFine:
        {
            m_controller.onChartSustainAdjustRequested(1, true);
            return true;
        }
        case EditorCommandId::SustainShortenFine:
        {
            m_controller.onChartSustainAdjustRequested(-1, true);
            return true;
        }
        case EditorCommandId::FretShiftUp:
        {
            m_controller.onChartFretShiftRequested(1);
            return true;
        }
        case EditorCommandId::FretShiftDown:
        {
            m_controller.onChartFretShiftRequested(-1);
            return true;
        }

        case EditorCommandId::NeutralInsert:
        {
            m_controller.onNeutralInsertRequested();
            return true;
        }

        // The Esc ladder: view-owned edge drags cancel first, then the marker ladder fires
        // when any published rung is live. An idle press is a silent no-op now that dispatch
        // is command-based; nothing downstream consumed the decoder's old fall-through.
        case EditorCommandId::CancelDismiss:
        {
            if (m_tone_automation_lanes_view.cancelActiveGesture())
            {
                return true;
            }
            if (m_tone_track_view.cancelActiveGesture())
            {
                return true;
            }
            if (m_state.chart_edit.marquee.has_value() ||
                m_state.tone_automation.drag_preview.has_value() ||
                m_state.chart_edit.caret.has_value() ||
                m_state.tone_automation.lane_caret.has_value() || m_state.selection_present)
            {
                m_controller.onChartEscapePressed();
            }
            return true;
        }

        // Digits type the row's payload (the typing rule, §9b), in the decoder's try order:
        // the lanes view first (it re-checks its own possibly gesture-deferred caret copy),
        // then chart fret typing; the passive marker keeps digits inert (the controller owns
        // that branch).
        case EditorCommandId::TypeDigit0:
        case EditorCommandId::TypeDigit1:
        case EditorCommandId::TypeDigit2:
        case EditorCommandId::TypeDigit3:
        case EditorCommandId::TypeDigit4:
        case EditorCommandId::TypeDigit5:
        case EditorCommandId::TypeDigit6:
        case EditorCommandId::TypeDigit7:
        case EditorCommandId::TypeDigit8:
        case EditorCommandId::TypeDigit9:
        {
            const int digit = static_cast<int>(info.commandID) -
                              static_cast<int>(toJuceCommandId(EditorCommandId::TypeDigit0));
            if (m_tone_automation_lanes_view.beginCaretValueEntry(digit))
            {
                return true;
            }
            if (chartShown())
            {
                m_controller.onChartFretDigitTyped(digit);
            }
            return true;
        }

        case EditorCommandId::GridFiner:
        {
            // Steps through the selector's listener, the same path as a combo selection, so
            // the controller still owns the applied value.
            m_grid_spacing_selector.stepNoteValue(1);
            return true;
        }
        case EditorCommandId::GridCoarser:
        {
            m_grid_spacing_selector.stepNoteValue(-1);
            return true;
        }
        case EditorCommandId::ZoomIn:
        {
            m_track_viewport->zoomByStep(1);
            return true;
        }
        case EditorCommandId::ZoomOut:
        {
            m_track_viewport->zoomByStep(-1);
            return true;
        }
    }
    return false;
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
        "Import Song",
        juce::File::getSpecialLocation(juce::File::userHomeDirectory),
        "*.rock;*.gp");

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

namespace
{

// Resolves where tone-file choosers start: the last-used tone directory, else the user home.
[[nodiscard]] juce::File toneChooserDirectory(const core::ToneDesignerViewState& state)
{
    if (!state.chooser_directory.empty())
    {
        const juce::File directory = common::core::juceFileFromPath(state.chooser_directory);
        if (directory.isDirectory())
        {
            return directory;
        }
    }

    return juce::File::getSpecialLocation(juce::File::userHomeDirectory);
}

} // namespace

// Opens an asynchronous tone-file chooser and forwards accepted selections to the controller.
void EditorView::showOpenToneChooser()
{
    m_file_chooser = std::make_unique<juce::FileChooser>(
        "Open Tone", toneChooserDirectory(m_state.tone_designer), "*.tone");

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

            safe_this->m_controller.onOpenToneFileRequested(common::core::pathFromJuceFile(file));
        });
}

// Opens an asynchronous tone-file save chooser prefilled with the current document name and
// forwards accepted destinations to the controller.
void EditorView::showToneSaveAsChooser(SaveAsChooserPurpose purpose)
{
    const juce::File initial_file =
        toneChooserDirectory(m_state.tone_designer)
            .getChildFile(juce::String{m_state.tone_designer.document_name} + ".tone");
    m_file_chooser = std::make_unique<juce::FileChooser>("Save Tone", initial_file, "*.tone");

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

            safe_this->m_controller.onSaveToneAsRequested(pathWithToneExtension(file));
        });
}

// Starts a fresh untitled designer document (the controller prompts for unsaved work first).
void EditorView::onNewTonePressed()
{
    m_controller.onNewToneRequested();
}

// Opens the tone-file chooser; the accepted selection flows through the controller's prompt gate.
void EditorView::onOpenToneFilePressed()
{
    showOpenToneChooser();
}

// Saves to the associated file, or routes an untitled document through the Save As chooser.
void EditorView::onSaveTonePressed()
{
    if (m_state.tone_designer.has_destination)
    {
        m_controller.onSaveToneRequested();
        return;
    }

    showToneSaveAsChooser(SaveAsChooserPurpose::UserSaveAs);
}

// Always asks for a destination, forking the document association on accept.
void EditorView::onSaveToneAsPressed()
{
    showToneSaveAsChooser(SaveAsChooserPurpose::UserSaveAs);
}

// Opens the import chooser; the controller confirms any automation drop before mutating.
void EditorView::onImportTonePressed()
{
    showImportToneChooser();
}

// Opens the export chooser prefilled with the active tone's catalog name.
void EditorView::onExportTonePressed()
{
    showExportToneChooser();
}

// Opens an asynchronous tone-file chooser and forwards accepted import selections.
void EditorView::showImportToneChooser()
{
    m_file_chooser = std::make_unique<juce::FileChooser>(
        "Import Tone", toneChooserDirectory(m_state.tone_designer), "*.tone");

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

            safe_this->m_controller.onImportToneFileRequested(common::core::pathFromJuceFile(file));
        });
}

// Opens an asynchronous tone-file save chooser prefilled with the active tone's name and
// forwards accepted export destinations.
void EditorView::showExportToneChooser()
{
    // The active (audible) tone is what exports; prefill the chooser with its catalog name.
    juce::String active_tone_name;
    for (const core::ToneRegionViewState& region : m_state.tone_track.regions)
    {
        if (region.active)
        {
            active_tone_name = juce::String{region.name};
            break;
        }
    }
    if (active_tone_name.isEmpty())
    {
        active_tone_name = "Tone";
    }

    const juce::File initial_file =
        toneChooserDirectory(m_state.tone_designer).getChildFile(active_tone_name + ".tone");
    m_file_chooser = std::make_unique<juce::FileChooser>("Export Tone", initial_file, "*.tone");

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

            safe_this->m_controller.onExportToneFileRequested(pathWithToneExtension(file));
        });
}

// Shows each distinct tone-import confirmation once and reports the selected decision.
void EditorView::presentToneImportPromptIfNeeded(
    const std::optional<core::ToneImportPrompt>& prompt)
{
    if (!prompt.has_value())
    {
        m_last_presented_tone_import_prompt.reset();
        return;
    }

    if (m_last_presented_tone_import_prompt == prompt)
    {
        return;
    }

    m_last_presented_tone_import_prompt = prompt;
    const juce::String parameter_count{static_cast<juce::int64>(
        prompt->automation_parameter_count)};
    const juce::String message =
        "Importing replaces this tone's rig and removes automation on " + parameter_count +
        (prompt->automation_parameter_count == 1 ? " parameter." : " parameters.");
    showThemedQuestionBox(
        this, "Import Tone", message, {"Import", "Cancel"}, [this](int button_index) {
            m_controller.onToneImportDecision(
                button_index == 0 ? core::ToneImportDecision::Import
                                  : core::ToneImportDecision::Cancel);
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
    showThemedQuestionBox(
        this,
        "Unsaved changes",
        unsavedChangesPromptMessage(prompt->prompted_action, m_state.tone_designer.active),
        {"Save", "Discard", "Cancel"},
        [this](int button_index) {
            switch (button_index)
            {
                case 0:
                {
                    m_controller.onUnsavedChangesDecision(core::UnsavedChangesDecision::Save);
                    break;
                }
                case 1:
                {
                    m_controller.onUnsavedChangesDecision(core::UnsavedChangesDecision::Discard);
                    break;
                }
                default:
                {
                    m_controller.onUnsavedChangesDecision(core::UnsavedChangesDecision::Cancel);
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
    // In designer mode the protective save targets the tone document, so the chooser is the tone
    // Save As dialog rather than the project one.
    if (m_state.tone_designer.active)
    {
        showToneSaveAsChooser(SaveAsChooserPurpose::DeferredAction);
        return;
    }
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
    showThemedQuestionBox(
        this,
        "Project did not finish opening",
        juce::String{"The previous project did not finish opening:\n\n"} +
            common::core::juceStringFromPath(prompt->project_file) + "\n\nTry opening it again?",
        {"OK", "Cancel"},
        [this](int button_index) {
            const core::RestoreInterruptedDecision decision =
                button_index == 0 ? core::RestoreInterruptedDecision::Retry
                                  : core::RestoreInterruptedDecision::Cancel;
            m_controller.onRestoreInterruptedDecision(decision);
        });
}

// Shows each distinct unavailable-game-audio notice once, then opens the audio device settings
// window so the user lands directly in the editable editor-own flow the startup fallback selected.
void EditorView::presentGameAudioUnavailablePromptIfNeeded(
    const std::optional<core::GameAudioUnavailablePrompt>& prompt)
{
    if (!prompt.has_value())
    {
        m_last_game_audio_unavailable_prompt.reset();
        return;
    }

    if (m_last_game_audio_unavailable_prompt == prompt)
    {
        return;
    }

    m_last_game_audio_unavailable_prompt = prompt;
    showThemedWarningBox(
        this,
        "Game audio settings unavailable",
        juce::String::fromUTF8(prompt->error.message.c_str()),
        [this] {
            m_controller.onGameAudioUnavailablePromptDismissed();
            showAudioDeviceSettingsWindow();
        });
}

// Opens the startup game-audio recommendation alert once per controller request and routes its
// single decision back; the self-deleting alert owns its own teardown, so the view only tracks
// whether the current request has been presented.
void EditorView::presentGameAudioRecommendationIfNeeded(bool prompt_requested)
{
    if (!prompt_requested)
    {
        m_game_audio_recommendation_presented = false;
        return;
    }

    if (m_game_audio_recommendation_presented)
    {
        return;
    }

    m_game_audio_recommendation_presented = true;
    GameAudioRecommendationDialog::show(
        *this, [this](core::GameAudioRecommendationDecision decision, bool suppress_future) {
            // The decline button reads "Open Audio Settings", so it lands the user in the audio
            // device settings window. The window opens BEFORE the decision is reported: the
            // settings-open state then suppresses the closed-device failure prompt that the
            // decision handler would otherwise stage under the opening window.
            if (decision == core::GameAudioRecommendationDecision::UseCustomSettings)
            {
                showAudioDeviceSettingsWindow();
            }
            m_controller.onGameAudioRecommendationDecision(decision, suppress_future);
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

    // The "use game audio settings" toggle governs both surfaces: while it is on, the calibration
    // popup is read-only -- the game's calibration value with a notice and no measure action -- to
    // match the device window's lock, which keys off the toggle alone. The toggle is only ever on
    // while a calibrated game configuration is adopted, so the reflected value always exists;
    // unchecking the toggle is the one way back to the editable editor-own flow.
    const bool read_only_game_reflection = m_state.use_game_audio_settings;

    if (m_input_calibration_window != nullptr)
    {
        m_input_calibration_window->setReadOnlyGameReflection(read_only_game_reflection);
        m_input_calibration_window->toFront(true);
        return;
    }

    m_input_calibration_window = std::make_unique<InputCalibrationWindow>(
        m_controller,
        &m_live_input,
        *prompt,
        isShowing() ? this : nullptr,
        read_only_game_reflection);
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

    m_audio_device_button.setEnabled(m_state.audio_device_settings_enabled);
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
        },
        GameAudioSettings{
            .use_game_settings = m_state.use_game_audio_settings,
            // Fresh one-shot read at open: NotConfigured disables the toggle with its tooltip.
            .source_state = m_controller.gameAudioSourceState(),
        },
        [safe_this](bool enabled, const std::function<void(bool)>& set_applying)
            -> std::expected<void, core::GameAudioSourceError> {
            if (auto* view = safe_this.getComponent())
            {
                return view->m_controller.onUseGameAudioSettingsChangeRequested(
                    enabled, set_applying);
            }

            // A torn-down view has no editor to switch, so there is no failure to report to the
            // closing dialog.
            return {};
        });
}

// Clears the owner-held settings window after JUCE and view callbacks have unwound. Destroying
// the window content runs the staged edit's cancel backstop synchronously, so once the reset has
// finished the device state is settled and the controller can trustworthily evaluate whether the
// editor ended up without an open device.
void EditorView::scheduleAudioDeviceSettingsWindowReset()
{
    const juce::Component::SafePointer<EditorView> safe_this{this};
    juce::MessageManager::callAsync([safe_this] {
        if (auto* view = safe_this.getComponent())
        {
            view->m_audio_device_settings_window.reset();
            view->m_audio_device_settings_window_reset_pending = false;
            view->m_controller.onAudioDeviceSettingsTeardownComplete();
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

// Routes a playhead-driven region crossing to the controller as an activation (no formal selection).
void EditorView::onToneRegionActivated()
{
    m_controller.onToneRegionActivated();
}

// Routes a committed tone-region resize to the controller.
void EditorView::onToneRegionResizeRequested(
    std::string region_id, common::core::GridPosition start, common::core::GridPosition end)
{
    m_controller.onToneRegionResizeRequested(std::move(region_id), start, end);
}

// Routes a committed tone-boundary move to the controller.
void EditorView::onToneBoundaryMoveRequested(
    std::string right_region_id, common::core::GridPosition position)
{
    m_controller.onToneBoundaryMoveRequested(std::move(right_region_id), position);
}

// Opens the tone picker for an Alt-click (or region-menu) tone-change insert at a position.
void EditorView::onToneChangeInsertRequested(common::core::GridPosition position)
{
    createToneMarkerAt(position);
}

// Routes a region-menu delete to the controller, exactly like the Delete key on a selection.
void EditorView::onToneRegionDeleteRequested(std::string region_id)
{
    m_controller.onToneRegionDeleteRequested(std::move(region_id));
}

void EditorView::onToneAutomationLaneAddRequested(std::string instance_id, std::string param_id)
{
    m_controller.onToneAutomationLaneAddRequested(std::move(instance_id), std::move(param_id));
}

void EditorView::onToneAutomationLaneRemoveRequested(std::string instance_id, std::string param_id)
{
    m_controller.onToneAutomationLaneRemoveRequested(std::move(instance_id), std::move(param_id));
}

void EditorView::onToneAutomationPointSelectRequested(
    std::string instance_id, std::string param_id, common::core::GridPosition position)
{
    m_controller.onToneAutomationPointSelectRequested(
        std::move(instance_id), std::move(param_id), position);
}

void EditorView::onToneAutomationPointerMove(const core::ToneAutomationPointerEvent& event)
{
    m_controller.onToneAutomationPointerMove(event);
}

void EditorView::onToneAutomationPointerExit()
{
    m_controller.onToneAutomationPointerExit();
}

void EditorView::onToneAutomationPointerDown(const core::ToneAutomationPointerEvent& event)
{
    m_controller.onToneAutomationPointerDown(event);
}

void EditorView::onToneAutomationPointerDrag(const core::ToneAutomationPointerEvent& event)
{
    m_controller.onToneAutomationPointerDrag(event);
}

void EditorView::onToneAutomationPointerUp(const core::ToneAutomationPointerEvent& event)
{
    m_controller.onToneAutomationPointerUp(event);
}

void EditorView::onToneAutomationPointsEditRequested(
    std::string instance_id, std::string param_id,
    std::vector<common::core::ToneAutomationPoint> points)
{
    m_controller.onToneAutomationPointsEditRequested(
        std::move(instance_id), std::move(param_id), std::move(points));
}

// Shows the tone-picker menu for inserting a tone-change marker at the playhead: the marker lands at
// the playhead's exact musical position inside the region it splits.
void EditorView::createToneMarkerAtCursor()
{
    // The marker rule ("one position concept per transport state" — the play-from-the-marker
    // unification, extended to Ctrl+T 2026-07-21): an armed caret IS the position, on either
    // row family, so the insert lands where play would pick up; the passive cursor is the
    // transport position. Positions are kept precisely rather than re-rounded to the nearest
    // whole beat — the fine-grid quantization preserves any position placed on a practical
    // subdivision (a grid line, or a Ctrl-free fine position).
    if (m_state.tone_automation.lane_caret.has_value())
    {
        createToneMarkerAt(m_state.tone_automation.lane_caret->position);
        return;
    }
    const double seconds = m_state.chart_edit.caret.has_value() ? m_state.chart_edit.caret->seconds
                                                                : m_transport.position().seconds;
    createToneMarkerAt(
        core::fineGridPositionForBeat(
            m_state.tempo_map, m_state.tempo_map.beatPositionAtSeconds(seconds)));
}

// Shows the tone-picker menu for inserting a tone-change marker at an exact musical position — the
// shared tail of the playhead accelerator (Ctrl+T) and the tone row's Alt-click/menu insert. The
// menu offers reusing an existing catalog tone (excluding the tones on either side of the new
// boundary, which would make it a no-op change) or minting a fresh empty one.
void EditorView::createToneMarkerAt(common::core::GridPosition position)
{
    // The marker splits the one region whose span strictly contains it; endpoints order by exact
    // musical position, so a marker landing on a boundary belongs to neither side and splits nothing.
    const auto containing = std::ranges::find_if(
        m_state.tone_track.regions, [&](const core::ToneRegionViewState& region) {
            return region.grid_start < position && position < region.grid_end;
        });
    if (containing == m_state.tone_track.regions.end())
    {
        return; // The marker fell on a boundary or outside any region; there is nothing to split.
    }

    // Exclude both the tone being split (the previous tone) and the tone immediately after the new
    // region (the next tone): choosing either would produce a boundary with no actual tone change on
    // that side. Offer every other distinct catalog tone, then a fresh-tone option.
    const auto containing_index =
        static_cast<std::size_t>(containing - m_state.tone_track.regions.begin());
    const std::string previous_ref = containing->tone_document_ref;
    const std::string next_ref =
        containing_index + 1 < m_state.tone_track.regions.size()
            ? m_state.tone_track.regions[containing_index + 1].tone_document_ref
            : std::string{};

    juce::PopupMenu menu;
    std::vector<std::string> reuse_refs;
    for (const core::ToneRegionViewState& region : m_state.tone_track.regions)
    {
        if (region.tone_document_ref.empty() || region.tone_document_ref == previous_ref ||
            (!next_ref.empty() && region.tone_document_ref == next_ref) ||
            std::ranges::find(reuse_refs, region.tone_document_ref) != reuse_refs.end())
        {
            continue;
        }
        reuse_refs.push_back(region.tone_document_ref);
        menu.addItem(
            static_cast<int>(reuse_refs.size()),
            "Use " + juce::String(region.name.empty() ? "tone" : region.name));
    }
    if (reuse_refs.empty())
    {
        // No other tone exists to reuse, so skip the picker and prompt for a fresh tone directly.
        promptForNewTone(position);
        return;
    }

    menu.addSeparator();
    const int create_new_id = static_cast<int>(reuse_refs.size()) + 1;
    menu.addItem(create_new_id, "New tone");

    menu.showMenuAsync(
        // Force a cancel result if this view is deleted while the menu is open, so the callback
        // never touches a dangling controller (JUCE reports result 0 for a deleted watch target).
        juce::PopupMenu::Options{}.withMousePosition().withDeletionCheck(*this),
        [this, position, reuse_refs, create_new_id](int result) {
            if (result == create_new_id)
            {
                promptForNewTone(position);
            }
            else if (result >= 1 && std::cmp_less_equal(result, reuse_refs.size()))
            {
                m_controller.onToneRegionCreateRequested(
                    position,
                    common::core::generatePackageId(),
                    reuse_refs[static_cast<std::size_t>(result - 1)]);
            }
        });
}

// Prompts for a new tone name (defaulting to "New Tone") and asks the controller to mint it at the
// marker; editor-core rejects and reports a duplicate name.
void EditorView::promptForNewTone(common::core::GridPosition position)
{
    showThemedTextPrompt(
        this,
        "New Tone",
        "Enter a name for the new tone:",
        "New Tone",
        "Create",
        [this, position](const juce::String& name) {
            const juce::String trimmed = name.trim();
            m_controller.onToneCreateNewRequested(
                position, (trimmed.isEmpty() ? juce::String{"New Tone"} : trimmed).toStdString());
        });
}

// Prompts for a new name for the double-clicked tone and forwards the rename to the controller.
void EditorView::onToneRenamePromptRequested(
    std::string tone_document_ref, std::string current_name)
{
    showThemedTextPrompt(
        this,
        "Rename Tone",
        "Enter a new name for this tone:",
        current_name,
        "Rename",
        [this, ref = std::move(tone_document_ref)](const juce::String& name) {
            m_controller.onToneRenameRequested(ref, name.toStdString());
        });
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
