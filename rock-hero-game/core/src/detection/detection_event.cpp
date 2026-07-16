#include <cstdint>
#include <rock_hero/game/core/detection/detection_event.h>
#include <variant>

namespace rock_hero::game::core
{

// Every alternative stores its ordering key under the same name, so one generic visitor reads it.
std::uint64_t inputStreamSampleOf(const DetectionEvent& event)
{
    return std::visit(
        [](const auto& alternative) { return alternative.input_stream_sample; }, event);
}

} // namespace rock_hero::game::core
