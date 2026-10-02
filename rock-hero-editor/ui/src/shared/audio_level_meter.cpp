#include "audio_level_meter.h"

#include "shared/editor_theme.h"
#include "shared/text_metrics.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <utility>

namespace rock_hero::editor::ui
{

namespace
{

constexpr double g_clip_hold_ms{1500.0};
constexpr int g_horizontal_label_width{48};
// The band above a captioned meter: the caption's line, then the arrow down to the bar.
constexpr int g_target_caption_height{20};
constexpr float g_target_caption_font_size{11.0f};
constexpr float g_target_arrow_height{6.0f};
constexpr double g_display_min_db{common::audio::minimumAudioMeterDb()};
constexpr double g_display_max_db{6.0};
constexpr double g_display_range_db{g_display_max_db - g_display_min_db};
const juce::Colour g_meter_background{juce::Colours::black.withAlpha(0.42f)};
const juce::Colour g_meter_border{juce::Colours::black.withAlpha(0.70f)};
const juce::Colour g_meter_low{juce::Colour{0xff45b86a}};
const juce::Colour g_meter_mid{juce::Colour{0xffd1b445}};
const juce::Colour g_meter_hot{juce::Colour{0xffd66a4a}};
const juce::Colour g_meter_clip{juce::Colour{0xfff04a4a}};

constexpr std::array<double, 10> g_horizontal_tick_db = {
    0.0,
    -6.0,
    -12.0,
    -18.0,
    -24.0,
    -30.0,
    -36.0,
    -42.0,
    -48.0,
    -54.0,
};
constexpr std::array<double, 5> g_vertical_tick_db = {0.0, -12.0, -24.0, -36.0, -48.0};
constexpr float g_tick_font_size{9.0f};
constexpr int g_horizontal_tick_min_center_gap{22};
constexpr int g_vertical_tick_min_center_gap{18};
const juce::Colour g_tick_label_color{juce::Colours::white.withAlpha(0.7f)};

// Maps a dB value to a display fraction across the full visual meter range (-60..+6).
[[nodiscard]] constexpr double displayFraction(double db) noexcept
{
    return std::clamp((db - g_display_min_db) / g_display_range_db, 0.0, 1.0);
}

// Selects a readable fill color based on peak level.
[[nodiscard]] juce::Colour fillColorForLevel(common::audio::AudioMeterLevel level)
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

// Draws dB labels with short fixed-length vertical tick lines above and below each number
// across a horizontal meter. Labels that would overlap are suppressed.
void drawHorizontalTickLabels(juce::Graphics& g, juce::Rectangle<int> inner)
{
    if (inner.getWidth() < 60 || inner.getHeight() < 14)
    {
        return;
    }

    const juce::Font tick_font{juce::FontOptions{g_tick_font_size}};
    g.setFont(tick_font);
    g.setColour(g_tick_label_color);
    constexpr int label_width = 22;
    constexpr int tick_length = 3;
    constexpr int tick_inset = 1;
    const int font_height = static_cast<int>(std::ceil(tick_font.getHeight()));
    const int text_y = (inner.getY() + inner.getBottom() - font_height) / 2;
    int last_drawn_x = inner.getRight() + g_horizontal_tick_min_center_gap + 1;

    for (const double tick_db : g_horizontal_tick_db)
    {
        const double fraction = displayFraction(tick_db);
        const int x = inner.getX() + static_cast<int>(std::round(inner.getWidth() * fraction));

        if (std::abs(x - last_drawn_x) < g_horizontal_tick_min_center_gap)
        {
            continue;
        }

        int label_x = x - label_width / 2;
        label_x = std::max(inner.getX(), std::min(label_x, inner.getRight() - label_width));

        g.drawVerticalLine(
            x,
            static_cast<float>(inner.getY() + tick_inset),
            static_cast<float>(inner.getY() + tick_inset + tick_length));

        g.drawText(
            juce::String{static_cast<int>(std::abs(tick_db))},
            label_x,
            text_y,
            label_width,
            font_height,
            juce::Justification::centred,
            false);

        g.drawVerticalLine(
            x,
            static_cast<float>(inner.getBottom() - tick_inset - tick_length),
            static_cast<float>(inner.getBottom() - tick_inset));

        last_drawn_x = x;
    }
}

// Draws dB labels with short fixed-length horizontal tick lines on either side of each number
// across a vertical meter. Labels that would overlap are suppressed.
void drawVerticalTickLabels(juce::Graphics& g, juce::Rectangle<int> inner)
{
    if (inner.getWidth() < 16 || inner.getHeight() < 40)
    {
        return;
    }

    const juce::Font tick_font{juce::FontOptions{g_tick_font_size}};
    g.setFont(tick_font);
    g.setColour(g_tick_label_color);
    constexpr int label_height = 12;
    constexpr int tick_length = 3;
    constexpr int tick_inset = 1;
    int last_drawn_y = inner.getY() - g_vertical_tick_min_center_gap - 1;

    for (const double tick_db : g_vertical_tick_db)
    {
        const double fraction = displayFraction(tick_db);
        const int y =
            inner.getBottom() - static_cast<int>(std::round(inner.getHeight() * fraction));

        if (std::abs(y - last_drawn_y) < g_vertical_tick_min_center_gap)
        {
            continue;
        }

        int label_y = y - label_height / 2;
        label_y = std::max(inner.getY(), std::min(label_y, inner.getBottom() - label_height));

        g.drawHorizontalLine(
            y,
            static_cast<float>(inner.getX() + tick_inset),
            static_cast<float>(inner.getX() + tick_inset + tick_length));

        g.drawText(
            juce::String{static_cast<int>(std::abs(tick_db))},
            inner.getX(),
            label_y,
            inner.getWidth(),
            label_height,
            juce::Justification::centred,
            false);

        g.drawHorizontalLine(
            y,
            static_cast<float>(inner.getRight() - tick_inset - tick_length),
            static_cast<float>(inner.getRight() - tick_inset));

        last_drawn_y = y;
    }
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

    level.peak_db = std::clamp(
        level.peak_db, common::audio::minimumAudioMeterDb(), common::audio::maximumAudioMeterDb());

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

void AudioLevelMeter::setTargetDb(std::optional<double> target_db)
{
    jassert(m_orientation == AudioLevelMeterOrientation::Horizontal);
    if (target_db != m_target_db)
    {
        m_target_db = target_db;
        repaint();
    }
}

void AudioLevelMeter::setTargetCaption(juce::String caption)
{
    jassert(m_orientation == AudioLevelMeterOrientation::Horizontal);
    m_target_caption = std::move(caption);
    repaint();
}

std::optional<double> AudioLevelMeter::targetDb() const noexcept
{
    return m_target_db;
}

// Draws a peak meter with dB tick marks. The display spans -60 to +6 dBFS so that 0 dB sits at
// its true position with a headroom zone beyond it reserved for the clipping indicator.
void AudioLevelMeter::paint(juce::Graphics& g)
{
    auto area = getLocalBounds().reduced(1);
    if (area.isEmpty())
    {
        return;
    }

    // A captioned meter keeps its caption band always, so the bar sits still as the mark comes
    // and goes; the band comes off first, so the label sits level with the bar.
    const juce::Rectangle<int> caption_band = m_target_caption.isNotEmpty()
                                                  ? area.removeFromTop(g_target_caption_height)
                                                  : juce::Rectangle<int>{};

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

    const auto inner = area.reduced(2);
    if (inner.isEmpty())
    {
        return;
    }

    const double capped_db = std::min(m_level.peak_db, common::audio::clippingAudioMeterDb());
    const double fill_fraction = displayFraction(capped_db);

    if (m_orientation == AudioLevelMeterOrientation::Horizontal)
    {
        const int fill_width = static_cast<int>(std::round(inner.getWidth() * fill_fraction));
        if (fill_width > 0)
        {
            g.setColour(fillColorForLevel(m_level));
            g.fillRect(inner.withWidth(fill_width));
        }
    }
    else
    {
        const int fill_height = static_cast<int>(std::round(inner.getHeight() * fill_fraction));
        if (fill_height > 0)
        {
            g.setColour(fillColorForLevel(m_level));
            g.fillRect(
                inner.getX(), inner.getBottom() - fill_height, inner.getWidth(), fill_height);
        }
    }

    if (m_orientation == AudioLevelMeterOrientation::Horizontal)
    {
        drawHorizontalTickLabels(g, inner);
    }
    else
    {
        drawVerticalTickLabels(g, inner);
    }

    // The target, in the accent: a mark across the meter where the owner wants the signal to land,
    // and its caption above with an arrow whose tip meets the bar at the mark's own x.
    const std::optional<double> target_db = m_target_db;
    if (target_db.has_value() && m_orientation == AudioLevelMeterOrientation::Horizontal)
    {
        constexpr int target_thickness{2};
        const int target_x =
            inner.getX() +
            static_cast<int>(std::round(inner.getWidth() * displayFraction(*target_db)));
        g.setColour(editorTheme().accent);
        g.fillRect(
            target_x - (target_thickness / 2), inner.getY(), target_thickness, inner.getHeight());

        if (!caption_band.isEmpty())
        {
            const auto tip_x = static_cast<float>(target_x);
            const auto tip_y = static_cast<float>(area.getY());
            juce::Path arrow;
            arrow.addTriangle(
                tip_x - (g_target_arrow_height / 2.0f),
                tip_y - g_target_arrow_height,
                tip_x + (g_target_arrow_height / 2.0f),
                tip_y - g_target_arrow_height,
                tip_x,
                tip_y);
            g.fillPath(arrow);

            // The caption centres on the arrow, kept inside the band near either end.
            const juce::Font caption_font{juce::FontOptions{g_target_caption_font_size}};
            g.setFont(caption_font);
            const int caption_width = textWidth(caption_font, m_target_caption);
            const int caption_x = std::clamp(
                target_x - (caption_width / 2),
                caption_band.getX(),
                std::max(caption_band.getX(), caption_band.getRight() - caption_width));
            g.drawText(
                m_target_caption,
                juce::Rectangle<int>{
                    caption_x,
                    caption_band.getY(),
                    caption_width,
                    caption_band.getHeight() - static_cast<int>(g_target_arrow_height),
                },
                juce::Justification::centred,
                false);
        }
    }

    if (!m_clip_indicator_active)
    {
        return;
    }

    const double clip_start = displayFraction(common::audio::clippingAudioMeterDb());
    g.setColour(g_meter_clip);
    if (m_orientation == AudioLevelMeterOrientation::Horizontal)
    {
        const int clip_x =
            inner.getX() + static_cast<int>(std::round(inner.getWidth() * clip_start));
        g.fillRect(clip_x, inner.getY(), inner.getRight() - clip_x, inner.getHeight());
    }
    else
    {
        const int clip_y =
            inner.getBottom() - static_cast<int>(std::round(inner.getHeight() * clip_start));
        g.fillRect(inner.getX(), inner.getY(), inner.getWidth(), clip_y - inner.getY());
    }
}

} // namespace rock_hero::editor::ui
