/*!
\file song_information_view.h
\brief The Song Information dialog's content: the song's title, artist, album and year fields.
*/

#pragma once

#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>
#include <rock_hero/common/core/song/song.h>

namespace rock_hero::editor::ui
{

/*!
\brief Four labelled fields and an OK / Cancel pair that edit one copy of the song's information.

Owns nothing but its fields: it opens on a seed, hands the fields' values to the submit callback
it was built with, as typed (the core trims and judges them), and reports a cancel to the cancel
callback. Enter in any field is OK and Escape in any field is Cancel. The year field takes up to
four digits, so a negative year cannot be typed; it shows a year of 0 (unknown) as blank, and
reads blank back as 0.
*/
class SongInformationView final : public juce::Component
{
public:
    /*!
    \brief Receives the fields' values when OK is pressed or Enter is typed in a field: the text
    exactly as typed and the year read from its digits, blank as 0.
    */
    using SubmitCallback = std::function<void(common::core::SongMetadata)>;

    /*! \brief Receives a cancel: the Cancel button, or Escape typed in a field. */
    using CancelCallback = std::function<void()>;

    /*!
    \brief Builds the fields showing the information the dialog opens on.
    \param seed The song's information as stored.
    \param on_submit Receives every submission; required.
    \param on_cancel Receives every cancel; required.
    */
    SongInformationView(
        const common::core::SongMetadata& seed, SubmitCallback on_submit, CancelCallback on_cancel);

    /*!
    \brief Moves keyboard focus to the title field with its whole text selected for replacement.

    Called once the dialog is up, so typing replaces the title straight away.
    */
    void focusTitle();

    /*! \brief Lays the four rows out above the button pair. */
    void resized() override;

private:
    // Hands the fields' current values to the submit callback.
    void submit();

    // Builds the label and field pair of one row and wires its Enter and Escape keys.
    void addRow(juce::Label& label, juce::TextEditor& field, const char* name, const char* id);

    // The two answers the view was built with; both always set.
    SubmitCallback m_on_submit;
    CancelCallback m_on_cancel;

    // One label and one field per row, in the focus order Title, Artist, Album, Year.
    juce::Label m_title_label;
    juce::TextEditor m_title;
    juce::Label m_artist_label;
    juce::TextEditor m_artist;
    juce::Label m_album_label;
    juce::TextEditor m_album;
    juce::Label m_year_label;
    juce::TextEditor m_year;

    // OK submits and Cancel cancels; focus reaches them after the year.
    juce::TextButton m_ok;
    juce::TextButton m_cancel;
};

} // namespace rock_hero::editor::ui
