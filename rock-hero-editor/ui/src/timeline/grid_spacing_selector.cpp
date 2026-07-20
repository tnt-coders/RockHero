#include "grid_spacing_selector.h"

#include <array>
#include <cstddef>
#include <optional>

namespace rock_hero::editor::ui
{

namespace
{

// Width reserved for the static caption so the combo box gets the remaining strip space.
constexpr int g_caption_width{36};

// Note-value presets offered as quick selections beside free fraction entry: the power-of-two
// ladder interleaved with the triplet subdivisions (1/6 = quarter triplets, 1/12 = eighth
// triplets, 1/24 = sixteenth triplets), which grid-native chart authoring needs within reach
// (settled 2026-07-18 — off-grid placement is gone, so tuplets come from tuplet grids). The
// raw-fraction labels are a recorded interim: friendlier REAPER-style names ("1/8 triplet")
// are a deferred decision in docs/plans/in-progress/editing-interaction-model.md.
constexpr std::array<common::core::Fraction, 9> g_note_value_presets{
    common::core::Fraction{1, 4},
    common::core::Fraction{1, 6},
    common::core::Fraction{1, 8},
    common::core::Fraction{1, 12},
    common::core::Fraction{1, 16},
    common::core::Fraction{1, 24},
    common::core::Fraction{1, 32},
    common::core::Fraction{1, 64},
    common::core::Fraction{1, 128},
};

// Formats a note value with the same syntax entry parses, so display and entry stay symmetric.
[[nodiscard]] juce::String noteValueText(common::core::Fraction note_value)
{
    return juce::String{note_value.numerator} + "/" + juce::String{note_value.denominator};
}

// Parses user text like "3/16" into a positive fraction, rejecting any other shape so garbage
// entry can never silently change the grid.
[[nodiscard]] std::optional<common::core::Fraction> parseNoteValueText(const juce::String& text)
{
    const juce::String trimmed = text.trim();
    const int slash_index = trimmed.indexOfChar('/');
    if (slash_index < 0)
    {
        return std::nullopt;
    }

    const juce::String numerator_text = trimmed.substring(0, slash_index).trim();
    const juce::String denominator_text = trimmed.substring(slash_index + 1).trim();
    if (numerator_text.isEmpty() || denominator_text.isEmpty() ||
        !numerator_text.containsOnly("0123456789") || !denominator_text.containsOnly("0123456789"))
    {
        return std::nullopt;
    }

    // getIntValue accumulates into int, so digit runs long enough to overflow it are rejected
    // before parsing; nine digits always fit and no valid note value needs more.
    if (numerator_text.length() > 9 || denominator_text.length() > 9)
    {
        return std::nullopt;
    }

    const int numerator = numerator_text.getIntValue();
    const int denominator = denominator_text.getIntValue();
    if (numerator < 1 || denominator < 1)
    {
        return std::nullopt;
    }

    return common::core::Fraction{numerator, denominator};
}

} // namespace

// Builds the caption and preset list; item ids are preset indices offset by one because JUCE
// reserves combo-box id zero for "nothing selected".
GridSpacingSelector::GridSpacingSelector(Listener& listener)
    : m_listener(listener)
{
    setComponentID("grid_spacing_selector");

    m_caption.setText("Grid", juce::dontSendNotification);
    m_caption.setJustificationType(juce::Justification::centredRight);
    m_caption.setInterceptsMouseClicks(false, false);
    addAndMakeVisible(m_caption);

    for (std::size_t index = 0; index < g_note_value_presets.size(); ++index)
    {
        m_note_value_box.addItem(
            noteValueText(g_note_value_presets.at(index)), static_cast<int>(index) + 1);
    }
    m_note_value_box.setEditableText(true);
    m_note_value_box.setComponentID("grid_note_value_box");
    m_note_value_box.onChange = [this] { handleSelectionCommitted(); };
    addAndMakeVisible(m_note_value_box);

    refreshDisplayedNoteValue();
}

// Applies the owner's note value; the notification-free refresh keeps state pushes from echoing
// back into the listener as selections.
void GridSpacingSelector::setNoteValue(common::core::Fraction note_value)
{
    m_note_value = note_value;
    refreshDisplayedNoteValue();
}

// Walks the preset ladder from the applied value in the requested direction (the +/- keyboard
// step). A free-entry value between presets snaps to the nearest preset in the step direction; when
// no preset lies strictly in that direction — already at or past the finest/coarsest preset — the
// step does nothing rather than snapping back against the requested direction (which a free-entered
// value past 1/128 or coarser than 1/4 would otherwise do). Presets and the applied value compare
// as exact rationals through Fraction's ordering. Emits through the listener so the applied value
// still changes only when the controller republishes it, exactly like a combo selection.
void GridSpacingSelector::stepNoteValue(int direction)
{
    if (direction == 0)
    {
        return;
    }

    if (direction > 0)
    {
        // Finer: the coarsest preset strictly smaller than the applied value (presets run
        // coarse -> fine, so the first match walking that order is the nearest finer preset).
        for (const common::core::Fraction preset : g_note_value_presets)
        {
            if (preset < m_note_value)
            {
                m_listener.onGridNoteValueChosen(preset);
                return;
            }
        }
        return;
    }

    // Coarser: the finest preset strictly larger than the applied value (scan fine -> coarse).
    for (auto preset = g_note_value_presets.rbegin(); preset != g_note_value_presets.rend();
         ++preset)
    {
        if (*preset > m_note_value)
        {
            m_listener.onGridNoteValueChosen(*preset);
            return;
        }
    }
}

// Gives the caption a fixed left band and the combo box the remaining strip space.
void GridSpacingSelector::resized()
{
    auto bounds = getLocalBounds();
    m_caption.setBounds(bounds.removeFromLeft(g_caption_width));
    m_note_value_box.setBounds(bounds.reduced(4, 0));
}

// Emits parsed entries and reverts the display otherwise; the accepted value comes back through
// setNoteValue when the controller republishes view state, so entry never self-applies.
void GridSpacingSelector::handleSelectionCommitted()
{
    const std::optional<common::core::Fraction> parsed =
        parseNoteValueText(m_note_value_box.getText());
    if (parsed.has_value() && *parsed != m_note_value)
    {
        m_listener.onGridNoteValueChosen(*parsed);
    }

    refreshDisplayedNoteValue();
}

// Displays the applied note value without notifications so refreshes cannot recurse into onChange.
void GridSpacingSelector::refreshDisplayedNoteValue()
{
    m_note_value_box.setText(noteValueText(m_note_value), juce::dontSendNotification);
}

} // namespace rock_hero::editor::ui
