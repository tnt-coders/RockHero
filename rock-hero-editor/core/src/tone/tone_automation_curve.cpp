#include "tone/tone_automation_curve.h"

#include <cmath>
#include <cstddef>

namespace rock_hero::editor::core
{

double toneAutomationAnchorSeconds()
{
    // The edit timeline's own zero, which is where writePluginParameterCurve puts the anchor it
    // prepends (tracktion::TimePosition{}). Stated as a time, not derived from the tempo map, so
    // the two seams cannot drift apart on a song whose first beat lands after zero.
    return 0.0;
}

common::core::GridPosition toneAutomationAnchorPosition()
{
    // Measure 1, beat 1, no offset — the earliest addressable slot, which is where a dragged
    // anchor plants its real point.
    return common::core::GridPosition{};
}

float toneAutomationCurveValueAtSeconds(
    ToneAutomationCurveSample anchor, std::span<const ToneAutomationCurveSample> authored,
    double seconds, bool is_discrete)
{
    // The anchor is the lane's implicit first sample unless an authored sample already occupies
    // (or precedes) its slot, in which case that sample is what the lane holds there and the
    // anchor contributes nothing — the audio write seam resolves the same collision by anchoring
    // on that sample's value.
    const bool anchored = authored.empty() || authored.front().seconds > anchor.seconds;
    const std::size_t count = authored.size() + (anchored ? 1U : 0U);
    const auto sample_at = [anchor, authored, anchored](std::size_t index) {
        if (anchored)
        {
            return index == 0 ? anchor : authored[index - 1];
        }
        return authored[index];
    };

    if (seconds <= sample_at(0).seconds)
    {
        return sample_at(0).norm_value;
    }
    if (seconds >= sample_at(count - 1).seconds)
    {
        return sample_at(count - 1).norm_value;
    }
    for (std::size_t index = 1; index < count; ++index)
    {
        const ToneAutomationCurveSample next = sample_at(index);
        if (seconds > next.seconds)
        {
            continue;
        }
        const ToneAutomationCurveSample previous = sample_at(index - 1);
        if (is_discrete)
        {
            // The drawn discrete curve holds the previous state until the next point.
            return previous.norm_value;
        }
        const double span = next.seconds - previous.seconds;
        const float mix =
            span > 0.0 ? static_cast<float>((seconds - previous.seconds) / span) : 1.0F;
        return std::lerp(previous.norm_value, next.norm_value, mix);
    }
    return sample_at(count - 1).norm_value;
}

} // namespace rock_hero::editor::core
