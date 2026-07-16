/*!
\file detection_event.h
\brief The detection pipeline's event vocabulary (plan 22 detection contract).
*/

#pragma once

#include <cstdint>
#include <rock_hero/game/core/detection/onset_event.h>
#include <rock_hero/game/core/detection/pitch_confirmation.h>
#include <rock_hero/game/core/detection/pitch_frame.h>
#include <variant>

namespace rock_hero::game::core
{

/*!
\brief Any event the detection pipeline publishes to its consumers.

Every alternative is a plain trivially-copyable value timestamped in input-stream sample time —
never wall-clock time — so event logs replay deterministically. Correlating stream time to song
time is explicitly outside this contract: scoring does it through the playback clock and the
calibration offsets.
*/
using DetectionEvent = std::variant<OnsetEvent, PitchFrame, PitchConfirmation>;

/*!
\brief Reads the stream-ordering key of any detection event.

Detection events are ordered by their position in the input device stream; this projection is
the ordering contract consumers sort and merge by.

\param event Event to read the ordering key from.
\return The event's input-stream sample position.
*/
[[nodiscard]] std::uint64_t inputStreamSampleOf(const DetectionEvent& event);

} // namespace rock_hero::game::core
