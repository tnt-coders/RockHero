#include "song_information/song_information_window.h"

#include "shared/editor_theme.h"
#include "song_information/song_information_view.h"

#include <memory>
#include <utility>

namespace rock_hero::editor::ui
{

// Native title bar and the editor's dialog background, like the audio-device settings window. The
// window joins the desktop at once, so its scale is the editor window's from the first open.
SongInformationWindow::SongInformationWindow(juce::Component& centre_around)
    : juce::DialogWindow(
          "Song Information", editorTheme().bar_background, true, true,
          juce::Component::getApproximateScaleFactorForComponent(&centre_around))
    , m_centre_around(&centre_around)
{
    setComponentID("song_information_window");
    setUsingNativeTitleBar(true);
    setResizable(false, false);
}

// Fresh fields for every open, so nothing typed into a cancelled dialog survives into the next.
// Replacing the content is safe here: an open is never reached from inside the old fields.
void SongInformationWindow::open(
    const common::core::SongMetadata& seed, SongInformationView::SubmitCallback on_submit,
    SongInformationView::CancelCallback on_cancel)
{
    m_on_submit = std::move(on_submit);
    m_on_cancel = std::move(on_cancel);

    auto fields = std::make_unique<SongInformationView>(
        seed,
        [this](common::core::SongMetadata metadata) { submit(std::move(metadata)); },
        [this] { cancel(); });
    const juce::Component::SafePointer<SongInformationView> safe_fields{fields.get()};
    setContentOwned(fields.release(), true);
    centreAroundComponent(m_centre_around.getComponent(), getWidth(), getHeight());
    setAlwaysOnTop(juce::WindowUtils::areThereAnyAlwaysOnTopWindows());
    enterModalState(true, nullptr, false);

    // enterModalState only TRIES to focus: on Windows the freshly shown peer is not yet the
    // focused native window, so the grab is dropped. One message-loop turn later the window is up
    // and active (the themed text prompt's reason, and its callAfterDelay rather than a callAsync
    // that could starve the first paint).
    juce::Timer::callAfterDelay(1, [safe_fields] {
        if (SongInformationView* const shown = safe_fields.getComponent(); shown != nullptr)
        {
            shown->focusTitle();
        }
    });
}

// The title bar's close is a cancel, never a silent hide.
void SongInformationWindow::closeButtonPressed()
{
    cancel();
}

// Overrides DialogWindow's hide-only Escape so it reaches the cancel callback too.
bool SongInformationWindow::escapeKeyPressed()
{
    cancel();
    return true;
}

// Each callback runs after the window closed, so whatever it does finds no dialog in the way. The
// guard is for a closed window, whose callbacks are already dropped.
void SongInformationWindow::submit(common::core::SongMetadata metadata)
{
    const SongInformationView::SubmitCallback answer = std::exchange(m_on_submit, {});
    close();
    if (answer)
    {
        answer(std::move(metadata));
    }
}

void SongInformationWindow::cancel()
{
    const SongInformationView::CancelCallback answer = std::exchange(m_on_cancel, {});
    close();
    if (answer)
    {
        answer();
    }
}

// Hides and ends modality without destroying anything, since this runs inside the fields' own
// callbacks.
void SongInformationWindow::close()
{
    m_on_submit = {};
    m_on_cancel = {};
    setVisible(false);
    if (isCurrentlyModal(false))
    {
        exitModalState(0);
    }
}

} // namespace rock_hero::editor::ui
