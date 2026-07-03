#include "musical_readout.h"

#include <cmath>
#include <utility>

namespace rock_hero::editor::ui
{

namespace
{

// Gap between the signature and the tempo marking, standing in for the two-space separator the
// previous single-label readout used.
constexpr int g_section_gap{12};

// Left inset matching juce::Label's default border so the readout aligns with the neighboring
// position label.
constexpr int g_left_inset{5};

// The quarter-note glyph is U+2669, supplied as escaped UTF-8 so source-file encoding cannot
// corrupt it; text shaping falls back to a symbol font when the UI font lacks the glyph.
[[nodiscard]] juce::String quarterNoteGlyph()
{
    return juce::String::fromUTF8("\xE2\x99\xA9");
}

// Measures text without using JUCE's deprecated Font string-width helpers.
[[nodiscard]] int textWidth(const juce::Font& font, const juce::String& text)
{
    juce::GlyphArrangement arrangement;
    arrangement.addLineOfText(font, text, 0.0f, 0.0f);
    return static_cast<int>(std::ceil(arrangement.getBoundingBox(0, -1, true).getWidth()));
}

} // namespace

// Builds the two faces from the shared readout height and names the component for tests. The
// glyph reuses the timeline ruler's 4:3 glyph-to-digit size ratio because an equal-size note
// symbol reads as illegibly small next to the digits.
MusicalReadout::MusicalReadout(float text_font_height)
    : m_text_font{juce::FontOptions{text_font_height, juce::Font::bold}}
    , m_glyph_font{juce::FontOptions{text_font_height * (4.0f / 3.0f), juce::Font::bold}}
{
    setComponentID("transport_musical_display");
    setInterceptsMouseClicks(false, false);
}

// Stores and repaints the readout values; unchanged values skip the repaint so display-cadence
// refreshes stay free while the transport holds position.
void MusicalReadout::setReadout(juce::String signature, juce::String tempo_digits)
{
    if (m_signature == signature && m_tempo_digits == tempo_digits)
    {
        return;
    }

    m_signature = std::move(signature);
    m_tempo_digits = std::move(tempo_digits);
    repaint();
}

// Returns the displayed values as one plain string, empty while blank.
juce::String MusicalReadout::text() const
{
    if (m_signature.isEmpty())
    {
        return {};
    }

    return m_signature + "  " + quarterNoteGlyph() + m_tempo_digits;
}

// Paints the signature, the enlarged quarter-note glyph, and the tempo digits left to right,
// each vertically centered so the enlarged glyph hangs evenly around the digit line.
void MusicalReadout::paint(juce::Graphics& g)
{
    if (m_signature.isEmpty())
    {
        return;
    }

    g.setColour(getLookAndFeel().findColour(juce::Label::textColourId));

    g.setFont(m_text_font);
    const int signature_width = textWidth(m_text_font, m_signature);
    g.drawText(
        m_signature,
        g_left_inset,
        0,
        signature_width,
        getHeight(),
        juce::Justification::centredLeft);

    const juce::String glyph = quarterNoteGlyph();
    const int glyph_x = g_left_inset + signature_width + g_section_gap;
    const int glyph_width = textWidth(m_glyph_font, glyph) + 1;
    g.setFont(m_glyph_font);
    g.drawText(glyph, glyph_x, 0, glyph_width, getHeight(), juce::Justification::centredLeft);

    g.setFont(m_text_font);
    g.drawText(
        m_tempo_digits,
        glyph_x + glyph_width,
        0,
        getWidth() - glyph_x - glyph_width,
        getHeight(),
        juce::Justification::centredLeft);
}

} // namespace rock_hero::editor::ui
