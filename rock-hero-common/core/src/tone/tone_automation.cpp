#include "rock_hero/common/core/tone/tone_automation.h"

namespace rock_hero::common::core
{

bool isValidToneParameterAutomation(
    const ToneParameterAutomation& automation, const TempoMap& tempo_map)
{
    if (automation.plugin_id.empty() || automation.param_id.empty() || automation.points.empty())
    {
        return false;
    }

    const ToneAutomationPoint* previous = nullptr;
    for (const ToneAutomationPoint& point : automation.points)
    {
        const GridPosition& position = point.position;
        const bool on_grid = position.measure >= 1 && position.beat >= 1 &&
                             position.beat <= tempo_map.beatsPerMeasureAt(position.measure) &&
                             position.offset.numerator >= 0 && position.offset < Fraction{1};
        if (!on_grid)
        {
            return false;
        }
        if (previous != nullptr && !(previous->position < position))
        {
            return false;
        }
        if (point.norm_value < 0.0F || point.norm_value > 1.0F || point.curve_shape < -1.0F ||
            point.curve_shape > 1.0F)
        {
            return false;
        }
        previous = &point;
    }
    return true;
}

} // namespace rock_hero::common::core
