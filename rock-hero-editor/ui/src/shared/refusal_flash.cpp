#include "shared/refusal_flash.h"

#include "shared/editor_theme.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace rock_hero::editor::ui
{

namespace
{

// The pulse train: how many times a refused element glows and how long the whole flash runs: two
// pulses, over a little more than half a second.
constexpr double g_refusal_flash_pulses{2.0};
constexpr double g_refusal_flash_seconds{0.6};

// How far the glow reaches past a mark's outline, as a fraction of the mark's size: the accent
// halo's own reach; a wider glow reads worse.
constexpr float g_refusal_glow_reach{0.2f};

// The glow's layers: nested strokes of the outline, each wider and each faint, which pile up to
// nearly the full colour at the outline and thin to one faint layer at the reach.
constexpr int g_refusal_glow_layers{4};
constexpr float g_refusal_glow_layer_alpha{0.4f};

// How far the train has run, in [0, 1].
[[nodiscard]] double pulseProgress(const double started_ms) noexcept
{
    return std::clamp(
        (juce::Time::getMillisecondCounterHiRes() - started_ms) /
            (g_refusal_flash_seconds * 1000.0),
        0.0,
        1.0);
}

} // namespace

RefusalPulse::RefusalPulse() noexcept
    : m_started_ms{juce::Time::getMillisecondCounterHiRes()}
{}

bool RefusalPulse::finished() const noexcept
{
    return pulseProgress(m_started_ms) >= 1.0;
}

// cos² over (pulses - 1/2) periods: lit at the refusal, dark between pulses, and dark again
// exactly as the train ends.
float RefusalPulse::level() const noexcept
{
    const double wave =
        std::cos(std::numbers::pi * (g_refusal_flash_pulses - 0.5) * pulseProgress(m_started_ms));
    return static_cast<float>(wave * wave);
}

// Even-odd over the outline and a box past the reach is the region outside the mark, so the glow
// stands around the mark like the accent's halo stands behind a head, from chrome drawn over it.
void paintRefusalGlow(
    juce::Graphics& g, const juce::Path& outline, const float extent, const float level)
{
    const float reach = extent * g_refusal_glow_reach;
    const juce::Graphics::ScopedSaveState saved{g};
    juce::Path outside = outline;
    outside.addRectangle(outline.getBounds().expanded(reach * 2.0f));
    outside.setUsingNonZeroWinding(false);
    g.reduceClipRegion(outside);
    g.setColour(
        editorTheme().invalid.withAlpha(level).withMultipliedAlpha(g_refusal_glow_layer_alpha));
    for (int layer = 1; layer <= g_refusal_glow_layers; ++layer)
    {
        // A stroke centred on the outline reaches half its width outside it.
        const float width =
            2.0f * reach * static_cast<float>(layer) / static_cast<float>(g_refusal_glow_layers);
        g.strokePath(outline, juce::PathStrokeType{width});
    }
}

} // namespace rock_hero::editor::ui
