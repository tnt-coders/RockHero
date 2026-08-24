#include "grid_spacing_selector.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <optional>
#include <ranges>

namespace rock_hero::editor::ui
{

namespace
{

// Width reserved for the static caption so the combo box gets the remaining strip space.
constexpr int g_caption_width{36};

// Thickness of the snap-off strike. One pixel is about 8% of the 12px digit ink height, which is
// the weight a printed strikethrough carries; 2px was sighted and rejected — at this size it reads
// as redaction, burying the digits instead of leaving them perfectly readable underneath.
constexpr float g_value_strike_thickness{1.0F};

// Note-value presets offered as quick selections beside free fraction entry: the power-of-two
// ladder interleaved with the triplet subdivisions (1/6 = quarter triplets, 1/12 = eighth
// triplets, 1/24 = sixteenth triplets), which grid-native chart authoring needs within reach.
// Turning snap off does not make these redundant: it quantizes to the tick, which is a lattice
// fine enough to disappear, not a musical subdivision a tuplet can be placed against. The
// raw-fraction labels are a recorded interim: friendlier REAPER-style names ("1/8 triplet") are a
// deferred decision in docs/plans/in-progress/editing-interaction-model.md.
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

// Finds the label a combo box draws its value through. JUCE builds that label from the
// look-and-feel and positions it there (juce_ComboBox.cpp, lookAndFeelChanged and resized), and it
// is the only child a combo box ever adds, so this is where the value's on-screen geometry lives.
[[nodiscard]] const juce::Label* comboBoxValueLabel(const juce::ComboBox& box)
{
    for (int index = 0; index < box.getNumChildComponents(); ++index)
    {
        if (const auto* label = dynamic_cast<const juce::Label*>(box.getChildComponent(index)))
        {
            return label;
        }
    }

    return nullptr;
}

// Bounds of the glyphs a label actually draws, in the label's own coordinates.
//
// Re-runs JUCE's fitted-text layout with the label's own drawing inputs — the same call
// LookAndFeel_V2::drawLabel makes — instead of guessing at the text's extent, so a mark aligned to
// this box cannot drift from the characters it is aligned to. The result is glyph-tight rather
// than the whole text field, which is the difference between marking the value and marking the
// control.
[[nodiscard]] juce::Rectangle<float> labelGlyphBounds(const juce::Label& label)
{
    const juce::Rectangle<float> text_area =
        label.getBorderSize().subtractedFrom(label.getLocalBounds()).toFloat();
    const juce::Font font = label.getFont();

    juce::GlyphArrangement arrangement;
    arrangement.addFittedText(
        font,
        label.getText(),
        text_area.getX(),
        text_area.getY(),
        text_area.getWidth(),
        text_area.getHeight(),
        label.getJustificationType(),
        std::max(1, static_cast<int>(text_area.getHeight() / font.getHeight())),
        label.getMinimumHorizontalScale());

    return arrangement.getBoundingBox(0, -1, false);
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

// Stores whether the displayed value binds placement and repaints the indicator when it changes.
// The control itself is never disabled: the grid stays selectable with snap off, and a disabled
// look would say the wrong thing.
void GridSpacingSelector::setSnapEnabled(const bool snap_enabled)
{
    if (m_snap_enabled == snap_enabled)
    {
        return;
    }

    m_snap_enabled = snap_enabled;
    repaint();
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
    for (const common::core::Fraction preset : std::views::reverse(g_note_value_presets))
    {
        if (preset > m_note_value)
        {
            m_listener.onGridNoteValueChosen(preset);
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

// The snap-off indicator: one 1px horizontal rule through the value's own digits, and nothing else.
//
// It marks the VALUE, never the control. An earlier version quieted the whole readout under a veil
// and ran the strike the width of the box; sighting rejected all of it. Darkening the surround
// framed the caption and the box in a shadow that belonged to neither, dimming the chrome said
// "unavailable" when the grid is still fully selectable, and a stroke reaching across the drop-down
// arrow read as "do not click this". A struck number says the one true thing — this figure is not
// binding right now — and leaves everything that is still live looking live.
//
// Horizontal rather than diagonal, and that is a grammar choice, not a taste one. A diagonal
// through a figure is the prohibition mark — the slash of a "no" sign — which says the value may
// not be used, when it may: the grid is still fully selectable. The horizontal rule is the
// strikethrough, which says a figure is no longer in force while leaving it perfectly readable,
// and that is exactly the state. The diagonal was also geometry that moved with the text, since
// its angle fell out of the value's own width — "1/4" struck steeply and "1/128" nearly flat —
// so one fixed state was drawn as a varying mark. At zero degrees every value is struck alike.
void GridSpacingSelector::paintOverChildren(juce::Graphics& g)
{
    if (m_snap_enabled)
    {
        return;
    }

    // A combo box always has its text label, so a miss here means JUCE changed shape underneath
    // us; the assertion says so in debug, and a shipped build draws no mark rather than a mark in
    // the wrong place.
    const juce::Label* value_label = comboBoxValueLabel(m_note_value_box);
    jassert(value_label != nullptr);
    if (value_label == nullptr)
    {
        return;
    }

    // Glyph bounds arrive in the label's coordinates; the label sits in the combo box and the box
    // sits in this component, so both origins carry the mark up to where it is painted.
    //
    // The line spans that box exactly, with nothing added at either end. The fitted box is the
    // text's ADVANCE box, so its edges already sit about a pixel outside the glyph ink — which is
    // the overhang a printed strikethrough has, arrived at for free. A tuning constant here would
    // only be a second, hand-kept opinion about a span the text metrics already state.
    const juce::Rectangle<float> glyph_box =
        labelGlyphBounds(*value_label)
            .translated(
                static_cast<float>(m_note_value_box.getX() + value_label->getX()),
                static_cast<float>(m_note_value_box.getY() + value_label->getY()));

    // One whole pixel row, and this is the whole trick. The fitted box's centre is fractional, and
    // a 1px line drawn there splits across two rows at roughly half coverage each — which is the
    // dimmed, half-strength treatment this indicator exists to avoid, arrived at by accident.
    // Rounding the top edge to a whole pixel puts all of the ink on the single row nearest that
    // centre, so the mark is as solid as the digits it crosses.
    //
    // The row comes from the fitted box and never from the value, so a fixed state is drawn in a
    // fixed place: the box spans the font's line box, whose vertical extent is a font metric rather
    // than a property of the characters, so its centre is the same at 1/4 and at 1/128. That centre
    // lands within half a pixel of the digits' own middle, because the ascent's headroom above the
    // digit tops and the descent's room below the baseline very nearly cancel; the row snap absorbs
    // what is left.
    const float strike_top = std::round(glyph_box.getCentreY() - (g_value_strike_thickness / 2.0F));

    // The digits' own ink at full strength, exactly like a pen through a printed price: the mark
    // is what carries the state, so weakening it would drift back toward the "unavailable" look
    // the indicator exists to avoid. The colour is read off the combo box, not off the editor
    // theme, because the strike's correctness condition is "the same ink as the digits underneath"
    // — the theme's primary_text is only the same white by coincidence of the look-and-feel's
    // scheme, and two places that have to agree by hand are a defect waiting to happen.
    g.setColour(m_note_value_box.findColour(juce::ComboBox::textColourId));
    g.fillRect(
        juce::Rectangle<float>{
            glyph_box.getX(), strike_top, glyph_box.getWidth(), g_value_strike_thickness
        });
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
