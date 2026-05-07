#include "editor_view.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <rock_hero/audio/i_thumbnail.h>
#include <rock_hero/core/audio_asset.h>
#include <utility>

namespace rock_hero::ui
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

// Ensures saved packages use the native Rock Hero extension when the chooser returns none.
[[nodiscard]] std::filesystem::path pathWithRhpExtension(const juce::File& file)
{
    std::filesystem::path path{file.getFullPathName().toWideCharPointer()};
    if (!path.empty() && path.extension().empty())
    {
        path.replace_extension(".rhp");
    }
    return path;
}

// Ensures published packages use the playable Rock Hero extension when the chooser returns none.
[[nodiscard]] std::filesystem::path pathWithRockExtension(const juce::File& file)
{
    std::filesystem::path path{file.getFullPathName().toWideCharPointer()};
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

    const auto& native_path = suggested_file.native();
    return juce::File{juce::String{native_path.c_str()}};
}

// Gives the unsaved-changes prompt enough context for the action that triggered it.
[[nodiscard]] juce::String unsavedChangesPromptMessage(PendingProjectAction action)
{
    switch (action)
    {
    case PendingProjectAction::Close:
        return "Save changes before closing the current project?";
    case PendingProjectAction::Open:
        return "Save changes before opening another project?";
    case PendingProjectAction::Import:
        return "Save changes before importing another project?";
    case PendingProjectAction::Exit:
        return "Save changes before exiting Rock Hero Editor?";
    }

    return "Save changes before continuing?";
}

} // namespace

// Converts a timeline position to a bounded subpixel coordinate for the cursor overlay.
std::optional<float> cursorXForTimelinePosition(
    core::TimePosition position, core::TimeRange visible_timeline, int width) noexcept
{
    const core::TimeDuration visible_duration = visible_timeline.duration();
    if (width <= 0 || visible_duration.seconds <= 0.0)
    {
        return std::nullopt;
    }

    const double relative_position =
        (position.seconds - visible_timeline.start.seconds) / visible_duration.seconds;
    const double clamped_position = std::clamp(relative_position, 0.0, 1.0);
    const auto max_x = static_cast<double>(width - 1);
    return static_cast<float>(clamped_position * max_x);
}

// Handles editor-wide timeline interaction and draws the cursor from live transport position.
class EditorView::CursorOverlay final : public juce::Component
{
public:
    // Starts vblank-driven cursor refresh against the injected read-only transport.
    CursorOverlay(IEditorController& controller, const audio::ITransport& transport)
        : m_controller(controller)
        , m_transport(transport)
        , m_vblank_attachment(this, [this] { advanceCursor(); })
    {
        setComponentID("cursor_overlay");
        setInterceptsMouseClicks(true, false);
    }

    // Stores discrete timeline mapping data pushed by EditorView::setState().
    void setVisibleTimelineRange(core::TimeRange visible_timeline) noexcept
    {
        m_visible_timeline = visible_timeline;
    }

    // Draws only the cursor; static waveform content remains in ArrangementView below it.
    void paint(juce::Graphics& g) override
    {
        if (!m_cursor_x.has_value())
        {
            return;
        }

        g.setColour(juce::Colours::white);
        g.drawLine(*m_cursor_x, 0.0f, *m_cursor_x, static_cast<float>(getHeight()), 2.0f);
    }

    // Converts editor-wide timeline clicks into normalized seek intent.
    void mouseDown(const juce::MouseEvent& event) override
    {
        if (getWidth() <= 0)
        {
            return;
        }

        const double ratio = static_cast<double>(event.x) / static_cast<double>(getWidth());
        m_controller.onWaveformClicked(std::clamp(ratio, 0.0, 1.0));
    }

private:
    // Samples the current position at render cadence and invalidates only changed cursor strips.
    void advanceCursor()
    {
        const auto next_cursor_x =
            cursorXForTimelinePosition(m_transport.position(), m_visible_timeline, getWidth());

        if (next_cursor_x == m_cursor_x)
        {
            return;
        }

        repaintCursorMovement(m_cursor_x, next_cursor_x);
        m_cursor_x = next_cursor_x;
    }

    // Invalidates the union of old/new subpixel cursor strips, including antialias padding.
    void repaintCursorMovement(
        std::optional<float> previous_cursor_x, std::optional<float> next_cursor_x)
    {
        if ((!previous_cursor_x.has_value() && !next_cursor_x.has_value()) || getWidth() <= 0 ||
            getHeight() <= 0)
        {
            return;
        }

        float left_x = 0.0f;
        float right_x = 0.0f;
        if (previous_cursor_x.has_value() && next_cursor_x.has_value())
        {
            left_x = std::min(*previous_cursor_x, *next_cursor_x);
            right_x = std::max(*previous_cursor_x, *next_cursor_x);
        }
        else
        {
            const float cursor_x =
                previous_cursor_x.has_value() ? *previous_cursor_x : *next_cursor_x;
            left_x = cursor_x;
            right_x = cursor_x;
        }
        constexpr int padding = 3;
        const int left = std::max(0, static_cast<int>(std::floor(left_x)) - padding);
        const int right = std::min(getWidth(), static_cast<int>(std::ceil(right_x)) + padding + 1);
        repaint(left, 0, right - left, getHeight());
    }

    // Controller receives editor-level timeline seek intent.
    IEditorController& m_controller;

    // Read-only transport source sampled at vblank cadence for its live position method.
    const audio::ITransport& m_transport;

    // Vblank-driven callback used to keep cursor motion smooth without transport listeners.
    juce::VBlankAttachment m_vblank_attachment;

    // Visible timeline range last pushed by EditorView::setState().
    core::TimeRange m_visible_timeline{};

    // Last subpixel cursor x coordinate drawn by the overlay, if a cursor is currently mappable.
    std::optional<float> m_cursor_x{};
};

// Creates child widgets and gives the arrangement view its waveform-thumbnail factory.
EditorView::EditorView(
    IEditorController& controller, const audio::ITransport& transport,
    audio::IThumbnailFactory& thumbnail_factory)
    : m_controller(controller)
    , m_menu_bar(this)
    , m_transport_controls(*this)
    , m_cursor_overlay(std::make_unique<CursorOverlay>(controller, transport))
{
    setWantsKeyboardFocus(true);

    m_menu_bar.setComponentID("file_menu_bar");
    m_transport_controls.setComponentID("transport_controls");
    m_arrangement_view.setComponentID("arrangement_view");

    m_arrangement_view.setThumbnailFactory(thumbnail_factory);

    addAndMakeVisible(m_menu_bar);
    addAndMakeVisible(m_transport_controls);
    addAndMakeVisible(m_arrangement_view);
    addAndMakeVisible(*m_cursor_overlay);
    m_arrangement_view.setVisible(m_state.project_loaded);
    m_cursor_overlay->setVisible(m_state.project_loaded);

    setSize(800, 300);
}

// Disconnects the menu bar from this model before base and member teardown begins.
EditorView::~EditorView()
{
    m_menu_bar.setModel(nullptr);
}

// Projects controller-derived state into child widgets and cursor mapping state.
void EditorView::setState(const EditorViewState& state)
{
    m_state = state;

    menuItemsChanged();
    m_arrangement_view.setVisible(m_state.project_loaded);
    m_cursor_overlay->setVisible(m_state.project_loaded);
    m_transport_controls.setState(
        TransportControlsState{
            .play_pause_enabled = m_state.play_pause_enabled,
            .stop_enabled = m_state.stop_enabled,
            .play_pause_shows_pause_icon = m_state.play_pause_shows_pause_icon,
        });

    m_arrangement_view.setVisibleTimeline(m_state.visible_timeline);
    m_arrangement_view.setState(m_state.arrangement);

    m_cursor_overlay->setVisibleTimelineRange(m_state.visible_timeline);
    presentErrorIfNeeded(m_state.last_error);
    presentUnsavedChangesPromptIfNeeded(m_state.unsaved_changes_prompt);
    presentSaveAsPromptIfNeeded(m_state.save_as_prompt);
    repaint();
}

// Paints the background and the no-project empty state when no waveform surface is visible.
void EditorView::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colours::darkgrey);

    if (!m_state.project_loaded)
    {
        g.setColour(juce::Colours::lightgrey);
        g.drawText("No Project Loaded", arrangementBounds(), juce::Justification::centred);
    }
}

// Keeps the control strip above the waveform view and overlays the cursor across that view.
void EditorView::resized()
{
    auto area = arrangementBounds();
    auto top_area = getLocalBounds().reduced(8);
    m_menu_bar.setBounds(top_area.removeFromTop(24));
    top_area.removeFromTop(8);
    auto transport_row = top_area.removeFromTop(32);
    m_transport_controls.setBounds(transport_row.removeFromLeft(176));
    m_arrangement_view.setBounds(area);
    m_cursor_overlay->setBounds(area);
    m_cursor_overlay->toFront(false);
}

// Routes editor-level keyboard shortcuts through the same controller intents as child widgets.
bool EditorView::keyPressed(const juce::KeyPress& key)
{
    if (key == juce::KeyPress{juce::KeyPress::spaceKey})
    {
        m_controller.onPlayPausePressed();
        return true;
    }

    return false;
}

// Returns the single File menu displayed by the editor.
juce::StringArray EditorView::getMenuBarNames()
{
    return {"File"};
}

// Builds the File menu using only controller-derived state.
juce::PopupMenu EditorView::getMenuForIndex(int top_level_menu_index, const juce::String& menu_name)
{
    if (top_level_menu_index != 0 || menu_name != "File")
    {
        return {};
    }

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

// Routes File menu selections to either a chooser or a direct controller intent.
void EditorView::menuItemSelected(int menu_item_id, int /*top_level_menu_index*/)
{
    switch (menu_item_id)
    {
    case g_open_command:
        if (m_state.open_enabled)
        {
            showOpenChooser();
        }
        break;
    case g_import_command:
        if (m_state.import_enabled)
        {
            showImportChooser();
        }
        break;
    case g_save_command:
        if (!m_state.save_enabled)
        {
            break;
        }
        if (m_state.save_requires_destination)
        {
            showSaveAsChooser(SaveAsChooserPurpose::UserCommand);
        }
        else
        {
            m_controller.onSaveRequested();
        }
        break;
    case g_save_as_command:
        if (m_state.save_as_enabled)
        {
            showSaveAsChooser(SaveAsChooserPurpose::UserCommand);
        }
        break;
    case g_publish_command:
        if (m_state.publish_enabled)
        {
            showPublishChooser();
        }
        break;
    case g_close_command:
        if (m_state.close_enabled)
        {
            m_controller.onCloseRequested();
        }
        break;
    case g_exit_command:
        m_controller.onExitRequested();
        break;
    default:
        break;
    }
}

// Opens an asynchronous file chooser and sends accepted native package paths to the controller.
void EditorView::showOpenChooser()
{
    m_file_chooser = std::make_unique<juce::FileChooser>(
        "Open Rock Hero package",
        juce::File::getSpecialLocation(juce::File::userHomeDirectory),
        "*.rhp");

    m_file_chooser->launchAsync(
        juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [this](const juce::FileChooser& chooser) {
            const auto file = chooser.getResult();
            if (!file.existsAsFile())
            {
                return;
            }

            m_controller.onOpenRequested(
                std::filesystem::path{file.getFullPathName().toWideCharPointer()});
        });
}

// Opens an asynchronous file chooser and sends accepted import paths to the controller.
void EditorView::showImportChooser()
{
    m_file_chooser = std::make_unique<juce::FileChooser>(
        "Import Rock Hero or Rocksmith package",
        juce::File::getSpecialLocation(juce::File::userHomeDirectory),
        "*.rock;*.psarc");

    m_file_chooser->launchAsync(
        juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [this](const juce::FileChooser& chooser) {
            const auto file = chooser.getResult();
            if (!file.existsAsFile())
            {
                return;
            }

            m_controller.onImportRequested(
                std::filesystem::path{file.getFullPathName().toWideCharPointer()});
        });
}

// Opens an asynchronous file chooser and sends accepted save paths to the controller.
void EditorView::showSaveAsChooser(SaveAsChooserPurpose purpose)
{
    m_file_chooser = std::make_unique<juce::FileChooser>(
        "Save Rock Hero package",
        juce::File::getSpecialLocation(juce::File::userHomeDirectory),
        "*.rhp");

    m_file_chooser->launchAsync(
        juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles |
            juce::FileBrowserComponent::warnAboutOverwriting,
        [this, purpose](const juce::FileChooser& chooser) {
            const auto file = chooser.getResult();
            if (file.getFullPathName().isEmpty())
            {
                if (purpose == SaveAsChooserPurpose::PendingProjectAction)
                {
                    m_controller.onSaveAsCancelled();
                }
                return;
            }

            m_controller.onSaveAsRequested(pathWithRhpExtension(file));
        });
}

// Opens an asynchronous file chooser and sends accepted playable publish paths to the controller.
void EditorView::showPublishChooser()
{
    m_file_chooser = std::make_unique<juce::FileChooser>(
        "Publish Rock Hero Song (.rock)",
        publishChooserInitialFile(m_state.suggested_publish_file),
        "*.rock");

    m_file_chooser->launchAsync(
        juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles |
            juce::FileBrowserComponent::warnAboutOverwriting,
        [this](const juce::FileChooser& chooser) {
            const auto file = chooser.getResult();
            if (file.getFullPathName().isEmpty())
            {
                return;
            }

            m_controller.onPublishRequested(pathWithRockExtension(file));
        });
}

// Shows each distinct error once and resets the edge when the controller clears the error.
void EditorView::presentErrorIfNeeded(const std::optional<std::string>& error)
{
    if (!error.has_value())
    {
        m_last_presented_error.reset();
        return;
    }

    if (m_last_presented_error == error)
    {
        return;
    }

    m_last_presented_error = error;
    juce::NativeMessageBox::showAsync(
        juce::MessageBoxOptions()
            .withIconType(juce::MessageBoxIconType::WarningIcon)
            .withTitle("Could not complete request")
            .withMessage(juce::String{error->c_str()})
            .withButton("OK"),
        nullptr);
}

// Shows each distinct unsaved-changes prompt once and reports the selected decision.
void EditorView::presentUnsavedChangesPromptIfNeeded(
    const std::optional<UnsavedChangesPrompt>& prompt)
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
            .withMessage(unsavedChangesPromptMessage(prompt->action))
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
                safe_this->m_controller.onUnsavedChangesDecision(UnsavedChangesDecision::Save);
                break;
            case 1:
                safe_this->m_controller.onUnsavedChangesDecision(UnsavedChangesDecision::Discard);
                break;
            default:
                safe_this->m_controller.onUnsavedChangesDecision(UnsavedChangesDecision::Cancel);
                break;
            }
        });
}

// Shows a controller-requested Save As chooser once and reports cancellation when needed.
void EditorView::presentSaveAsPromptIfNeeded(const std::optional<SaveAsPrompt>& prompt)
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
    showSaveAsChooser(SaveAsChooserPurpose::PendingProjectAction);
}

// Mirrors resized() layout so paint() can draw the empty-project message in the content area.
juce::Rectangle<int> EditorView::arrangementBounds() const
{
    auto area = getLocalBounds().reduced(8);
    area.removeFromTop(24);
    area.removeFromTop(8);
    area.removeFromTop(32);
    area.removeFromTop(8);
    return area;
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

} // namespace rock_hero::ui
