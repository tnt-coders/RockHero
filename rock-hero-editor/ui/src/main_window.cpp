#include "main_window.h"

#include <rock_hero/common/audio/engine.h>
#include <rock_hero/editor/ui/editor_settings.h>
#include <rock_hero/ui/editor.h>

namespace rock_hero::editor::ui
{

// Owns the editor runtime dependencies before creating the editor feature that stores references.
MainWindow::MainWindow(const juce::String& title)
    : juce::DocumentWindow(
          title,
          juce::Desktop::getInstance().getDefaultLookAndFeel().findColour(
              juce::ResizableWindow::backgroundColourId),
          juce::DocumentWindow::allButtons)
    , m_settings(std::make_unique<EditorSettings>())
    , m_audio_engine(std::make_unique<rock_hero::common::audio::Engine>())
{
    // Engine implements each editor-facing audio port. Passing it for each role keeps Editor
    // dependent on narrow interfaces rather than on the concrete Tracktion adapter.
    m_editor = std::make_unique<rock_hero::ui::Editor>(
        *m_audio_engine,
        *m_audio_engine,
        *m_audio_engine,
        [this](std::optional<std::filesystem::path> project_file) {
            closeWindow(std::move(project_file));
        });

    setUsingNativeTitleBar(true);
    setContentNonOwned(&m_editor->component(), true);
    setResizable(true, false);
    centreWithSize(1280, 800);
    setVisible(true);
    restoreLastOpenProject();
}

// Removes JUCE's non-owning pointers before owned content and engine members are destroyed.
MainWindow::~MainWindow()
{
    // Null out DocumentWindow's non-owning pointer before m_editor is destroyed.
    // Otherwise ~ResizableWindow would call removeChildComponent on a dangling pointer.
    clearContentComponent();
}

// Routes the native close button through the same guarded exit flow as File > Exit.
void MainWindow::closeButtonPressed()
{
    requestExit();
}

// Routes platform quit requests through the same guarded exit flow as the close button.
void MainWindow::requestExit()
{
    if (m_editor != nullptr)
    {
        m_editor->requestExit();
        return;
    }

    if (auto* app = juce::JUCEApplicationBase::getInstance(); app != nullptr)
    {
        app->quit();
    }
}

// Attempts to reopen the project that was loaded when the previous editor process exited.
void MainWindow::restoreLastOpenProject()
{
    if (m_editor == nullptr || m_settings == nullptr)
    {
        return;
    }

    const std::optional<std::filesystem::path> project_file = m_settings->lastOpenProject();
    if (!project_file.has_value())
    {
        return;
    }

    std::error_code error;
    if (!std::filesystem::is_regular_file(*project_file, error))
    {
        m_settings->setLastOpenProject(std::nullopt);
        return;
    }

    m_editor->openProject(*project_file);
    const std::optional<std::filesystem::path> opened_project = m_editor->currentProjectFile();
    if (!opened_project.has_value() ||
        opened_project->lexically_normal() != project_file->lexically_normal())
    {
        m_settings->setLastOpenProject(std::nullopt);
    }
}

// Saves app-local restore state, then runs normal JUCE application shutdown.
void MainWindow::closeWindow(std::optional<std::filesystem::path> project_file)
{
    if (m_settings != nullptr)
    {
        m_settings->setLastOpenProject(std::move(project_file));
    }

    if (auto* app = juce::JUCEApplicationBase::getInstance(); app != nullptr)
    {
        app->quit();
    }
}

} // namespace rock_hero::editor::ui
