#include "tone/tone_automation.h"

#include <rock_hero/common/core/chart/chart_rules.h>

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
        if (!isValidGridPosition(position, tempo_map))
        {
            return false;
        }
        if (previous != nullptr && !(previous->position < position))
        {
            return false;
        }
        // Written as ranges the value must be INSIDE, not as the excursions it must avoid, because
        // every comparison against a NaN is false: the excursion form accepted NaN, and the writer
        // then emitted the bare token `nan`, which is not JSON — so one NaN from a hosted plugin
        // made song.json permanently unparseable and the project unopenable. This is the only
        // validator between a captured point and the file.
        if (!(point.norm_value >= 0.0F && point.norm_value <= 1.0F) ||
            !(point.curve_shape >= -1.0F && point.curve_shape <= 1.0F))
        {
            return false;
        }
        previous = &point;
    }
    return true;
}

} // namespace rock_hero::common::core
