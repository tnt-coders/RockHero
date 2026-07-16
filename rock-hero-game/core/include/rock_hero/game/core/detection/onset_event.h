/*!
\file onset_event.h
\brief Detected note-start event in the live input stream (plan 22 detection contract).
*/

#pragma once

#include <compare>
#include <cstdint>
#include <type_traits>

namespace rock_hero::game::core
{

/*! \brief Spectral character of a detected onset. */
enum class OnsetCharacter
{
    /*! \brief The onset leads into periodic, pitched signal. */
    Pitched,

    /*! \brief The onset is percussive and unpitched — the class that scores full mutes. */
    Percussive,

    /*! \brief The detector could not classify the onset's spectral character. */
    Unknown
};

/*! \brief How the detector observed a note start. */
enum class OnsetOrigin
{
    /*! \brief A physical attack transient (picked, plucked, popped, slapped, or muted hit). */
    Transient,

    /*!
    \brief A discrete step in the tracked pitch with no attack transient — legato playing.

    Hammer-ons, pull-offs, and taps often produce no transient a flux detector can see; the
    sustained-pitch tracker publishes the note start instead, back-dated to the first frame of
    the new pitch. A pitch-step onset always carries Pitched character, is exempt from the
    transient-onset latency target (it is bounded by the new pitch's register row in the
    confirmation-budget table instead), and its strength is the step-decision confidence.
    */
    PitchStep
};

/*!
\brief One detected note start, timestamped in input-stream sample time.

Onsets are published as soon as the note start is detected and never wait for pitch evidence —
the fast-onset/slow-pitch split is what makes scoring's provisional-hit state machine work.
Timestamps estimate the physical note-start position (back-dated for pitch-step onsets), are
never wall-clock time, and live on the pipeline's continuous input stream (detection_event.h
documents the stream and its device-restart semantics); correlating them to song time is the
scoring consumer's job through the playback clock and the calibration offsets. Transients
within the strum-coalescing window belong to one gesture and publish one onset, timestamped at
the first transient.
*/
struct OnsetEvent
{
    /*! \brief Estimated note-start position in the pipeline's input stream, in samples. */
    std::uint64_t input_stream_sample{0};

    /*! \brief Input-stream sample rate in Hz, making the timestamp self-describing on replay. */
    double sample_rate_hz{0.0};

    /*!
    \brief Normalized onset strength in [0, 1].

    Strength distributions are origin-specific (flux magnitude for transients, step-decision
    confidence for pitch steps) and detector-defined but stable within one detection version, so
    ruleset thresholds against strength are tuned per origin and per detection version.
    */
    float strength{0.0F};

    /*! \brief Spectral character classification of the note start. */
    OnsetCharacter character{OnsetCharacter::Unknown};

    /*! \brief How this note start was observed; PitchStep implies Pitched character. */
    OnsetOrigin origin{OnsetOrigin::Transient};

    /*!
    \brief Compares two onset events by their stored fields.
    \param lhs Left-hand onset event.
    \param rhs Right-hand onset event.
    \return True when both events store equal values.
    */
    friend constexpr bool operator==(const OnsetEvent& lhs, const OnsetEvent& rhs) noexcept
    {
        // Hand-written, not defaulted: a defaulted comparison trips clang's -Wfloat-equal on the
        // floating members. Exact equality is intended; the ordering query expresses it warning-
        // free with identical semantics (NaN compares unequal either way).
        return lhs.input_stream_sample == rhs.input_stream_sample &&
               std::is_eq(lhs.sample_rate_hz <=> rhs.sample_rate_hz) &&
               std::is_eq(lhs.strength <=> rhs.strength) && lhs.character == rhs.character &&
               lhs.origin == rhs.origin;
    }
};

// Detection events cross lock-free queues and serialize into plan-23 replay logs, so they must
// stay plain trivially-copyable values with no handles or owning members.
static_assert(std::is_trivially_copyable_v<OnsetEvent>);

} // namespace rock_hero::game::core
