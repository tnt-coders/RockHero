#include "audio_level_meter.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace rock_hero::editor::ui
{

namespace
{

constexpr double g_clip_hold_ms{1500.0};
constexpr int g_horizontal_label_width{48};
const juce::Colour g_meter_background{juce::Colours::black.withAlpha(0.42f)};
const juce::Colour g_meter_border{juce::Colours::black.withAlpha(0.70f)};
const juce::Colour g_meter_low{juce::Colour{0xff45b86a}};
const juce::Colour g_meter_mid{juce::Colour{0xffd1b445}};
const juce::Colour g_meter_hot{juce::Colour{0xffd66a4a}};
const juce::Colour g_meter_clip{juce::Colour{0xfff04a4a}};

// Selects a readable fill colour based on peak level.
[[nodiscard]] juce::Colour fillColourForLevel(common::audio::AudioMeterLevel level)
{
    if (level.clipping || level.peak_db >= -3.0)
    {
        return g_meter_hot;
    }

    if (level.peak_db >= -12.0)
    {
        return g_meter_mid;
    }

    return g_meter_low;
}

} // namespace

// Stores the drawing mode and avoids keyboard focus in compact control strips.
AudioLevelMeter::AudioLevelMeter(AudioLevelMeterOrientation orientation, juce::String label)
    : m_orientation(orientation)
    , m_label(std::move(label))
{
    setWantsKeyboardFocus(false);
    setInterceptsMouseClicks(false, false);
}

// Updates the peak value and holds the clip indicator long enough to be visible.
void AudioLevelMeter::setLevel(common::audio::AudioMeterLevel level)
{
    if (!std::isfinite(level.peak_db))
    {
        level.peak_db = common::audio::minimumAudioMeterDb();
    }

    level.peak_db = std::clamp(level.peak_db, common::audio::minimumAudioMeterDb(), 12.0);

    const common::audio::AudioMeterLevel previous_level = m_level;
    const bool previous_clip_state = m_clip_indicator_active;
    const double now_ms = juce::Time::getMillisecondCounterHiRes();

    m_level = level;
    if (level.clipping)
    {
        m_clip_indicator_active = true;
        m_last_clip_time_ms = now_ms;
    }
    else if (m_clip_indicator_active && now_ms - m_last_clip_time_ms > g_clip_hold_ms)
    {
        m_clip_indicator_active = false;
    }

    if (previous_level != m_level || previous_clip_state != m_clip_indicator_active)
    {
        repaint();
    }
}

// Returns the raw level last supplied by the owning view.
common::audio::AudioMeterLevel AudioLevelMeter::level() const noexcept
{
    return m_level;
}

// Draws a simple peak meter. The red clip marker is deliberately separate from the level fill so
// users can spot a clipped block even after the peak falls.
void AudioLevelMeter::paint(juce::Graphics& g)
{
    auto area = getLocalBounds().reduced(1);
    if (area.isEmpty())
    {
        return;
    }

    if (m_orientation == AudioLevelMeterOrientation::Horizontal && m_label.isNotEmpty())
    {
        const auto label_area =
            area.removeFromLeft(std::min(g_horizontal_label_width, area.getWidth()));
        g.setColour(juce::Colours::white.withAlpha(0.85f));
        g.setFont(juce::FontOptions{12.0f});
        g.drawFittedText(m_label, label_area, juce::Justification::centred, 1);
    }

    if (area.isEmpty())
    {
        return;
    }

    g.setColour(g_meter_background);
    g.fillRect(area);
    g.setColour(g_meter_border);
    g.drawRect(area);

    const double fraction = common::audio::audioMeterFraction(m_level.peak_db);
    auto fill_area = area.reduced(2);
    if (m_orientation == AudioLevelMeterOrientation::Horizontal)
    {
        fill_area.setWidth(static_cast<int>(std::round(fill_area.getWidth() * fraction)));
    }
    else
    {
        const int fill_height = static_cast<int>(std::round(fill_area.getHeight() * fraction));
        fill_area = fill_area.removeFromBottom(fill_height);
    }

    if (!fill_area.isEmpty())
    {
        g.setColour(fillColourForLevel(m_level));
        g.fillRect(fill_area);
    }

    if (!m_clip_indicator_active)
    {
        return;
    }

    g.setColour(g_meter_clip);
    if (m_orientation == AudioLevelMeterOrientation::Horizontal)
    {
        g.fillRect(area.removeFromRight(std::min(5, area.getWidth())));
    }
    else
    {
        g.fillRect(area.removeFromTop(std::min(5, area.getHeight())));
    }
}

} // namespace rock_hero::editor::ui
