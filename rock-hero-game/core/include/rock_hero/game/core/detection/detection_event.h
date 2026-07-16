/*!
\file detection_event.h
\brief The detection pipeline's event vocabulary (plan 22 detection contract).
*/

#pragma once

#include <cstdint>
#include <rock_hero/game/core/detection/onset_event.h>
#include <rock_hero/game/core/detection/pitch_confirmation.h>
#include <rock_hero/game/core/detection/pitch_frame.h>
#include <rock_hero/game/core/detection/polyphonic_salience.h>
#include <type_traits>
#include <variant>

namespace rock_hero::game::core
{

/*!
\brief Any event the detection pipeline publishes to its consumers.

Every alternative is a plain trivially-copyable value timestamped in input-stream sample time —
never wall-clock time — so event logs replay deterministically. Correlating stream time to song
time is explicitly outside this contract: scoring does it through the playback clock and the
calibration offsets.

Timestamps live on the pipeline's continuous virtual input stream: if the input device restarts
mid-session (the tap's generation counter bumps), the pipeline rebases so event positions stay
monotonic without a discontinuity. Whether a run survives a device restart is the session's
decision, not this vocabulary's — the events themselves never carry a generation.
*/
using DetectionEvent = std::variant<OnsetEvent, PitchFrame, PitchConfirmation, PolyphonicSalience>;

// The variant itself crosses the pipeline's lock-free queue, so the composite — not just each
// alternative — must stay trivially copyable; a future non-trivial alternative breaks it here.
static_assert(std::is_trivially_copyable_v<DetectionEvent>);

/*!
\brief Reads the stream-ordering key of any detection event.

Detection events are ordered by their position in the input device stream; this projection is
the ordering contract consumers sort and merge by.

\param event Event to read the ordering key from.
\return The event's input-stream sample position.
*/
[[nodiscard]] std::uint64_t inputStreamSampleOf(const DetectionEvent& event);

} // namespace rock_hero::game::core
