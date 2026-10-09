#include <rock_hero/common/core/chart/chart_rules.h>
#include <rock_hero/common/core/timeline/grid_position.h>
#include <rock_hero/common/core/timeline/time_signature_text.h>
#include <rock_hero/editor/core/timeline/time_signature_target.h>

namespace rock_hero::editor::core
{

TimeSignatureChordTarget timeSignatureTargetAt(
    const common::core::TempoMap& tempo_map, const common::core::GridPosition beat)
{
    const common::core::MeterReign governing = tempo_map.timeSignatureAt(beat.measure);
    const std::string meter_text = common::core::timeSignatureText(governing.meter);
    if (beat == common::core::measureDownbeat(governing.first_measure))
    {
        return SetTimeSignatureTarget{.position = beat, .meter_text = meter_text};
    }
    if (!common::core::markerCanStartAt(beat, tempo_map))
    {
        return {};
    }
    return InsertTimeSignatureTarget{.position = beat, .meter_text = meter_text};
}

} // namespace rock_hero::editor::core
