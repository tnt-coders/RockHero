/*!
\file song_information_window.h
\brief The modal File > Song Information dialog window.
*/

#pragma once

#include "song_information/song_information_view.h"

#include <juce_gui_basics/juce_gui_basics.h>
#include <rock_hero/common/core/song/song.h>

namespace rock_hero::editor::ui
{

/*!
\brief The modal window hosting the Song Information dialog's fields.

Kept by its owner and reopened for every request, so it is never destroyed from inside one of its
own callbacks: each open builds fresh fields on the seed, and each answer — OK, Enter, Cancel,
Escape, the title bar's close — hides the window and ends its modality BEFORE it reaches its
callback. The window writes nothing itself; whatever the answer means is the callback's.
*/
class SongInformationWindow final : public juce::DialogWindow
{
public:
    /*!
    \brief Builds the hidden window, themed like every editor dialog.
    \param centre_around Component every open centres the window on, and whose display scale it
           takes: the editor's top-level window. Watched, so it may be destroyed first.
    */
    explicit SongInformationWindow(juce::Component& centre_around);

    /*!
    \brief Shows the dialog modally with fresh fields on a seed.

    \param seed The song's information as stored, which the fields open on.
    \param on_submit Receives OK's or Enter's answer once the window has closed; required.
    \param on_cancel Receives a cancel once the window has closed; required.
    */
    void open(
        const common::core::SongMetadata& seed, SongInformationView::SubmitCallback on_submit,
        SongInformationView::CancelCallback on_cancel);

    /*! \brief Answers the title bar's close button as a cancel. */
    void closeButtonPressed() override;

    /*!
    \brief Answers Escape outside the fields (on a button) as a cancel.
    \return True: the key is consumed.
    */
    bool escapeKeyPressed() override;

private:
    // Hides the window, ends its modality and hands the fields' values to the stored callback.
    void submit(common::core::SongMetadata metadata);

    // Hides the window, ends its modality and hands the cancel to the stored callback.
    void cancel();

    // Hides the window and ends its modality, dropping both callbacks so a stale one can never
    // answer a later open.
    void close();

    // The component every open centres on.
    juce::Component::SafePointer<juce::Component> m_centre_around;

    // The answers of the open dialog; empty while the window is closed.
    SongInformationView::SubmitCallback m_on_submit;
    SongInformationView::CancelCallback m_on_cancel;
};

} // namespace rock_hero::editor::ui
