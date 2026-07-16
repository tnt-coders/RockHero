#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <rock_hero/game/core/detection/detection_event.h>
#include <vector>

namespace rock_hero::game::core
{

namespace
{

// A fully populated onset used as the equality baseline; copies are field-tweaked per check.
[[nodiscard]] OnsetEvent makeOnset(std::uint64_t input_stream_sample)
{
    return OnsetEvent{
        .input_stream_sample = input_stream_sample,
        .sample_rate_hz = 48000.0,
        .strength = 0.75F,
        .character = OnsetCharacter::Pitched,
    };
}

} // namespace

// Detection events are plain values: equality compares every stored field exactly, so replayed
// event logs can be verified byte-for-byte against a live run.
TEST_CASE("Detection events compare as values", "[core][detection]")
{
    const OnsetEvent onset = makeOnset(100);
    CHECK(onset == makeOnset(100));

    OnsetEvent different_strength = makeOnset(100);
    different_strength.strength = 0.5F;
    CHECK_FALSE(onset == different_strength);

    OnsetEvent different_character = makeOnset(100);
    different_character.character = OnsetCharacter::Percussive;
    CHECK_FALSE(onset == different_character);

    const PitchFrame frame{
        .input_stream_sample = 200,
        .sample_rate_hz = 48000.0,
        .f0_hz = 82.4,
        .confidence = 0.9F,
        .clarity = 0.8F,
        .rms = 0.25F,
    };
    CHECK(frame == PitchFrame{frame});

    PitchFrame different_f0 = frame;
    different_f0.f0_hz = 110.0;
    CHECK_FALSE(frame == different_f0);

    const PitchConfirmation confirmation{
        .input_stream_sample = 300,
        .sample_rate_hz = 48000.0,
        .onset_stream_sample = 100,
        .span_begin_sample = 220,
        .f0_hz = 82.4,
        .confidence = 0.95F,
    };
    CHECK(confirmation == PitchConfirmation{confirmation});

    PitchConfirmation different_onset = confirmation;
    different_onset.onset_stream_sample = 150;
    CHECK_FALSE(confirmation == different_onset);
}

// The stream-ordering contract: heterogeneous events sort by their input-stream sample position
// through the inputStreamSampleOf projection, regardless of event kind.
TEST_CASE("Detection events order by input stream sample position", "[core][detection]")
{
    std::vector<DetectionEvent> events{
        PitchConfirmation{
            .input_stream_sample = 300,
            .sample_rate_hz = 48000.0,
            .onset_stream_sample = 100,
            .span_begin_sample = 220,
            .f0_hz = 82.4,
            .confidence = 0.95F,
        },
        makeOnset(100),
        PitchFrame{
            .input_stream_sample = 200,
            .sample_rate_hz = 48000.0,
            .f0_hz = 82.4,
            .confidence = 0.9F,
            .clarity = 0.8F,
            .rms = 0.25F,
        },
    };

    std::ranges::sort(events, {}, inputStreamSampleOf);

    REQUIRE(events.size() == 3);
    CHECK(inputStreamSampleOf(events[0]) == 100);
    CHECK(inputStreamSampleOf(events[1]) == 200);
    CHECK(inputStreamSampleOf(events[2]) == 300);
    CHECK(std::holds_alternative<OnsetEvent>(events[0]));
    CHECK(std::holds_alternative<PitchFrame>(events[1]));
    CHECK(std::holds_alternative<PitchConfirmation>(events[2]));
}

} // namespace rock_hero::game::core
