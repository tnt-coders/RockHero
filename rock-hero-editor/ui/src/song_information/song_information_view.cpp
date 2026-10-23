#include "song_information/song_information_view.h"

#include "shared/editor_theme.h"

#include <array>
#include <string>
#include <utility>

namespace rock_hero::editor::ui
{

namespace
{

// The view's layout, in pixels: an outer margin, one row per field, and the button row below.
constexpr int g_margin{16};
constexpr int g_row_height{28};
constexpr int g_row_gap{8};
constexpr int g_label_width{64};
constexpr int g_field_width{320};
constexpr int g_button_width{88};
constexpr int g_row_count{4};

// Four digits holds every year a song is released in; digits only, so no sign can be typed.
constexpr int g_year_digits{4};

// Shows a year of 0 (unknown) as an empty field rather than a literal zero.
[[nodiscard]] juce::String yearText(const int year)
{
    return year > 0 ? juce::String{year} : juce::String{};
}

} // namespace

// Builds the rows in focus order and seeds them; the buttons close the order.
SongInformationView::SongInformationView(
    const common::core::SongMetadata& seed, SubmitCallback on_submit, CancelCallback on_cancel)
    : m_on_submit{std::move(on_submit)}
    , m_on_cancel{std::move(on_cancel)}
{
    setComponentID("song_information_view");
    addRow(m_title_label, m_title, "Title", "song_information_title");
    addRow(m_artist_label, m_artist, "Artist", "song_information_artist");
    addRow(m_album_label, m_album, "Album", "song_information_album");
    addRow(m_year_label, m_year, "Year", "song_information_year");
    m_year.setInputRestrictions(g_year_digits, "0123456789");

    m_title.setText(juce::String{seed.title}, juce::dontSendNotification);
    m_artist.setText(juce::String{seed.artist}, juce::dontSendNotification);
    m_album.setText(juce::String{seed.album}, juce::dontSendNotification);
    m_year.setText(yearText(seed.year), juce::dontSendNotification);

    m_ok.setButtonText("OK");
    m_ok.setComponentID("song_information_ok");
    m_ok.onClick = [this] { submit(); };
    addAndMakeVisible(m_ok);
    m_cancel.setButtonText("Cancel");
    m_cancel.setComponentID("song_information_cancel");
    m_cancel.onClick = [this] { m_on_cancel(); };
    addAndMakeVisible(m_cancel);

    // Tab walks the fields top to bottom and then the buttons, left to right.
    const std::array<juce::Component*, 6> focus_order{
        &m_title, &m_artist, &m_album, &m_year, &m_ok, &m_cancel
    };
    int order = 1;
    for (juce::Component* const component : focus_order)
    {
        component->setExplicitFocusOrder(order++);
    }
    setFocusContainerType(FocusContainerType::keyboardFocusContainer);

    setSize(
        (2 * g_margin) + g_label_width + g_field_width,
        (2 * g_margin) + ((g_row_count + 1) * g_row_height) + (g_row_count * g_row_gap));
}

// Selecting the whole title lets the first keystroke replace it.
void SongInformationView::focusTitle()
{
    m_title.grabKeyboardFocus();
    m_title.selectAll();
}

// One row per field, the label left of its field, and the buttons right-aligned beneath.
void SongInformationView::resized()
{
    juce::Rectangle<int> area = getLocalBounds().reduced(g_margin);
    const std::array<std::pair<juce::Label*, juce::TextEditor*>, g_row_count> rows{{
        {&m_title_label, &m_title},
        {&m_artist_label, &m_artist},
        {&m_album_label, &m_album},
        {&m_year_label, &m_year},
    }};
    for (const auto& [label, field] : rows)
    {
        juce::Rectangle<int> row = area.removeFromTop(g_row_height);
        label->setBounds(row.removeFromLeft(g_label_width));
        field->setBounds(row);
        area.removeFromTop(g_row_gap);
    }

    juce::Rectangle<int> buttons = area.removeFromTop(g_row_height);
    m_cancel.setBounds(buttons.removeFromRight(g_button_width));
    buttons.removeFromRight(g_row_gap);
    m_ok.setBounds(buttons.removeFromRight(g_button_width));
}

// The text crosses the UI seam as UTF-8, untrimmed: the core's normalization owns the trim.
void SongInformationView::submit()
{
    m_on_submit(
        common::core::SongMetadata{
            .title = m_title.getText().toStdString(),
            .artist = m_artist.getText().toStdString(),
            .album = m_album.getText().toStdString(),
            .year = m_year.getText().getIntValue(),
        });
}

// Every field is one line, so Enter answers the dialog rather than breaking the line.
void SongInformationView::addRow(
    juce::Label& label, juce::TextEditor& field, const char* name, const char* id)
{
    label.setText(name, juce::dontSendNotification);
    label.setColour(juce::Label::textColourId, editorTheme().primary_text);
    addAndMakeVisible(label);

    field.setComponentID(id);
    field.setMultiLine(false);
    field.onReturnKey = [this] { submit(); };
    field.onEscapeKey = [this] { m_on_cancel(); };
    addAndMakeVisible(field);
}

} // namespace rock_hero::editor::ui
