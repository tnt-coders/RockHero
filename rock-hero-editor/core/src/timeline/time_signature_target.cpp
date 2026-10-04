#include <rock_hero/common/core/chart/chart_rules.h>
#include <rock_hero/common/core/timeline/grid_position.h>
#include <rock_hero/editor/core/timeline/time_signature_target.h>
#include <rock_hero/editor/core/timeline/time_signature_text.h>

namespace rock_hero::editor::core
{

TimeSignatureChordTarget timeSignatureTargetAt(
    const common::core::TempoMap& tempo_map, const int measure)
{
    const common::core::TimeSignatureChange governing = tempo_map.timeSignatureAt(measure);
    if (governing.measure == measure)
    {
        return SetTimeSignatureTarget{
            .measure = measure,
            .meter_text = timeSignatureText(governing.meter),
        };
    }
    if (!common::core::markerCanStartAt(common::core::measureDownbeat(measure), tempo_map))
    {
        return {};
    }
    return InsertTimeSignatureTarget{
        .measure = measure,
        .meter_text = timeSignatureText(governing.meter),
    };
}

} // namespace rock_hero::editor::core
