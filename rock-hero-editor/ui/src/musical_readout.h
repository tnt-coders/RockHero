#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace rock_hero::editor::ui
{

// Read-only transport-strip readout drawing the active time signature and quarter-note tempo,
// such as "4/4 ♩=120.00". Hand-paints its text instead of using juce::Label because the tempo
// marking mixes fonts: the quarter-note glyph draws enlarged like the timeline ruler's tempo
// markings, and one label (one text draw) cannot mix fonts.
class MusicalReadout final : public juce::Component
{
public:
    // Builds the text and glyph faces from the shared transport-readout font height and names
    // the component for tests.
    explicit MusicalReadout(float text_font_height);

    // Stores and repaints the readout values. An empty signature blanks the readout; unchanged
    // values skip the repaint so display-cadence refreshes stay free.
    void setReadout(juce::String signature, juce::String tempo_digits);

    // Returns the displayed values as one plain string ("4/4  ♩=120.00"), empty while blank.
    // Exists for tests, which cannot read the painted runs any other way.
    [[nodiscard]] juce::String text() const;

    // Paints the signature, the enlarged quarter-note glyph, and the tempo digits left to right.
    void paint(juce::Graphics& g) override;

private:
    // Readout text face shared with the neighboring transport labels.
    juce::Font m_text_font;

    // Enlarged bold face for the quarter-note glyph, reusing the timeline ruler's glyph-to-digit
    // size ratio.
    juce::Font m_glyph_font;

    // Active time signature as "numerator/denominator"; empty blanks the whole readout.
    juce::String m_signature{};

    // Tempo digits drawn after the glyph, including the leading equals sign ("=120.00").
    juce::String m_tempo_digits{};
};

} // namespace rock_hero::editor::ui
