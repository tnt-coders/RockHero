#include "tone/tone_schedule.h"

#include <algorithm>
#include <cstddef>
#include <span>
#include <string>
#include <vector>

namespace rock_hero::common::core
{

std::vector<ToneSwitchRegion> makeToneSchedule(
    const ToneTrack& tone_track, const TempoMap& tempo_map, TimeDuration song_length)
{
    std::vector<ToneSwitchRegion> schedule;
    if (tone_track.regions.empty())
    {
        return schedule;
    }

    schedule.reserve(tone_track.regions.size());
    for (std::size_t index = 0; index < tone_track.regions.size(); ++index)
    {
        const ToneRegion& region = tone_track.regions[index];

        // The first region owns the lead-in: it extends back to the timeline origin because no
        // one-based grid position can address time before measure 1 (mirrors the editor's
        // display projection in tone_track_projection.cpp).
        const double start_seconds =
            index == 0 ? 0.0
                       : tempo_map.secondsAtNote(
                             region.start.measure, region.start.beat, region.start.offset);

        // A region ends where the next begins; the final span extends to the end of the loaded
        // content rather than to the tempo map's terminal, so the last tone plays out the audio.
        double end_seconds = 0.0;
        if (index + 1 < tone_track.regions.size())
        {
            const ToneRegion& next = tone_track.regions[index + 1];
            end_seconds =
                tempo_map.secondsAtNote(next.start.measure, next.start.beat, next.start.offset);
        }
        else
        {
            end_seconds = song_length.seconds;
        }

        // Defensive floor: a malformed track (or zero-length content) can never produce a span
        // that ends before it starts; downstream automation baking assumes forward spans.
        end_seconds = std::max(end_seconds, start_seconds);

        schedule.push_back(
            ToneSwitchRegion{
                .time_range =
                    TimeRange{
                        .start = TimePosition{start_seconds},
                        .end = TimePosition{end_seconds},
                    },
                .tone_document_ref = region.tone_document_ref,
            });
    }

    return schedule;
}

std::vector<ToneGainPoint> makeToneGainEnvelope(
    std::span<const ToneSwitchRegion> schedule, const std::string& tone_document_ref,
    double ramp_seconds)
{
    std::vector<ToneGainPoint> envelope;

    // The origin point is always explicit so a curve exists even for a never-referenced tone
    // (a single silent point) and so playback has a defined value before the first boundary.
    const bool opens_audible =
        !schedule.empty() && schedule.front().tone_document_ref == tone_document_ref;
    envelope.push_back(ToneGainPoint{.seconds = 0.0, .gain = opens_audible ? 1.0F : 0.0F});

    for (std::size_t index = 1; index < schedule.size(); ++index)
    {
        const ToneSwitchRegion& incoming = schedule[index];
        const ToneSwitchRegion& outgoing = schedule[index - 1];

        // A boundary between two spans of the SAME tone is a re-strike, not a switch: baking
        // points there would dip the branch to silence mid-note.
        if (incoming.tone_document_ref == outgoing.tone_document_ref)
        {
            continue;
        }

        const bool is_outgoing = outgoing.tone_document_ref == tone_document_ref;
        const bool is_incoming = incoming.tone_document_ref == tone_document_ref;
        if (!is_outgoing && !is_incoming)
        {
            continue;
        }

        // The crossfade lives at the head of the incoming span; clamping it to half that span
        // keeps back-to-back short spans from overlapping their fades.
        const double boundary = incoming.time_range.start.seconds;
        const double incoming_length =
            incoming.time_range.end.seconds - incoming.time_range.start.seconds;
        const double ramp = std::min(ramp_seconds, incoming_length / 2.0);

        envelope.push_back(ToneGainPoint{.seconds = boundary, .gain = is_outgoing ? 1.0F : 0.0F});
        envelope.push_back(
            ToneGainPoint{.seconds = boundary + ramp, .gain = is_incoming ? 1.0F : 0.0F});
    }

    // The closing anchor, unconditional so EVERY branch reaches the backend with two or more
    // points. Tracktion treats a one-point curve as no automation at all
    // (AutomationIterator::isEmpty() is points.size() <= 1) and, worse, silently REWRITES that lone
    // point to follow a message-thread gain write (tracktion_AutomatableParameter.cpp:1436-1440),
    // so a tone the schedule never names would have its envelope edited by the direct audible-tone
    // write. The authored-lane writer anchors for exactly this pair of reasons
    // (tone_automation_curve.cpp). Repeating the gain already reached keeps the closing segment
    // flat, and the schedule's end is never earlier than the last crossfade point, whose span the
    // ramp clamp keeps inside its own region.
    const double schedule_end = schedule.empty() ? 0.0 : schedule.back().time_range.end.seconds;
    const float closing_gain = envelope.back().gain;
    envelope.push_back(ToneGainPoint{.seconds = schedule_end, .gain = closing_gain});

    return envelope;
}

} // namespace rock_hero::common::core
