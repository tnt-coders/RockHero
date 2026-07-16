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
        .origin = OnsetOrigin::Transient,
    };
}

// A fully populated salience snapshot; two ranked candidates, unused slots contractually zeroed.
[[nodiscard]] PolyphonicSalience makeSalience(std::uint64_t input_stream_sample)
{
    PolyphonicSalience salience{
        .input_stream_sample = input_stream_sample,
        .sample_rate_hz = 48000.0,
        .onset_stream_sample = 100,
        .candidate_count = 2,
        .candidates = {},
    };
    salience.candidates[0] = SaliencePeak{.f0_hz = 82.4, .salience = 0.9F};
    salience.candidates[1] = SaliencePeak{.f0_hz = 123.5, .salience = 0.6F};
    return salience;
}

} // namespace

// Detection events are plain values: equality compares every stored field exactly (field for
// field), so replayed event logs can be verified against a live run. Every field of every type
// is tweaked once — a field added to a struct but forgotten in its hand-written operator==
// fails here instead of passing silently.
TEST_CASE("Detection events compare as values", "[core][detection]")
{
    const OnsetEvent onset = makeOnset(100);
    CHECK(onset == makeOnset(100));
    {
        OnsetEvent tweaked = makeOnset(100);
        tweaked.input_stream_sample = 101;
        CHECK_FALSE(onset == tweaked);
    }
    {
        OnsetEvent tweaked = makeOnset(100);
        tweaked.sample_rate_hz = 44100.0;
        CHECK_FALSE(onset == tweaked);
    }
    {
        OnsetEvent tweaked = makeOnset(100);
        tweaked.strength = 0.5F;
        CHECK_FALSE(onset == tweaked);
    }
    {
        OnsetEvent tweaked = makeOnset(100);
        tweaked.character = OnsetCharacter::Percussive;
        CHECK_FALSE(onset == tweaked);
    }
    {
        OnsetEvent tweaked = makeOnset(100);
        tweaked.origin = OnsetOrigin::PitchStep;
        CHECK_FALSE(onset == tweaked);
    }

    const PitchFrame frame{
        .input_stream_sample = 200,
        .sample_rate_hz = 48000.0,
        .f0_hz = 82.4,
        .confidence = 0.9F,
        .clarity = 0.8F,
        .rms = 0.25F,
    };
    CHECK(frame == PitchFrame{frame});
    {
        PitchFrame tweaked = frame;
        tweaked.input_stream_sample = 201;
        CHECK_FALSE(frame == tweaked);
    }
    {
        PitchFrame tweaked = frame;
        tweaked.sample_rate_hz = 44100.0;
        CHECK_FALSE(frame == tweaked);
    }
    {
        PitchFrame tweaked = frame;
        tweaked.f0_hz = 110.0;
        CHECK_FALSE(frame == tweaked);
    }
    {
        PitchFrame tweaked = frame;
        tweaked.confidence = 0.1F;
        CHECK_FALSE(frame == tweaked);
    }
    {
        PitchFrame tweaked = frame;
        tweaked.clarity = 0.1F;
        CHECK_FALSE(frame == tweaked);
    }
    {
        PitchFrame tweaked = frame;
        tweaked.rms = 0.5F;
        CHECK_FALSE(frame == tweaked);
    }

    const PitchConfirmation confirmation{
        .input_stream_sample = 300,
        .sample_rate_hz = 48000.0,
        .onset_stream_sample = 100,
        .span_begin_sample = 220,
        .f0_hz = 82.4,
        .confidence = 0.95F,
    };
    CHECK(confirmation == PitchConfirmation{confirmation});
    {
        PitchConfirmation tweaked = confirmation;
        tweaked.input_stream_sample = 301;
        CHECK_FALSE(confirmation == tweaked);
    }
    {
        PitchConfirmation tweaked = confirmation;
        tweaked.sample_rate_hz = 44100.0;
        CHECK_FALSE(confirmation == tweaked);
    }
    {
        PitchConfirmation tweaked = confirmation;
        tweaked.onset_stream_sample = 150;
        CHECK_FALSE(confirmation == tweaked);
    }
    {
        PitchConfirmation tweaked = confirmation;
        tweaked.span_begin_sample = 230;
        CHECK_FALSE(confirmation == tweaked);
    }
    {
        PitchConfirmation tweaked = confirmation;
        tweaked.f0_hz = 110.0;
        CHECK_FALSE(confirmation == tweaked);
    }
    {
        PitchConfirmation tweaked = confirmation;
        tweaked.confidence = 0.5F;
        CHECK_FALSE(confirmation == tweaked);
    }
}

// The chord-evidence snapshot compares its whole candidate array — unused slots are
// contractually zeroed, so a producer that leaves garbage past candidate_count fails equality
// instead of leaking nondeterminism into replay logs.
TEST_CASE("Polyphonic salience compares candidates and zeroed tail slots", "[core][detection]")
{
    const PolyphonicSalience salience = makeSalience(400);
    CHECK(salience == makeSalience(400));
    {
        PolyphonicSalience tweaked = makeSalience(400);
        tweaked.input_stream_sample = 401;
        CHECK_FALSE(salience == tweaked);
    }
    {
        PolyphonicSalience tweaked = makeSalience(400);
        tweaked.sample_rate_hz = 44100.0;
        CHECK_FALSE(salience == tweaked);
    }
    {
        PolyphonicSalience tweaked = makeSalience(400);
        tweaked.onset_stream_sample = 150;
        CHECK_FALSE(salience == tweaked);
    }
    {
        PolyphonicSalience tweaked = makeSalience(400);
        tweaked.candidate_count = 1;
        CHECK_FALSE(salience == tweaked);
    }
    {
        PolyphonicSalience tweaked = makeSalience(400);
        tweaked.candidates[1].salience = 0.5F;
        CHECK_FALSE(salience == tweaked);
    }
    {
        // A dirty slot past candidate_count still breaks equality — the zeroed-tail contract.
        PolyphonicSalience tweaked = makeSalience(400);
        tweaked.candidates[7].f0_hz = 1.0;
        CHECK_FALSE(salience == tweaked);
    }
}

// Replay verification compares whole DetectionEvents: the variant is equal only when the held
// alternative and its fields match, and different kinds never compare equal even when their
// stream positions coincide.
TEST_CASE("Detection event variants compare as values", "[core][detection]")
{
    const DetectionEvent onset{makeOnset(100)};
    CHECK(onset == DetectionEvent{makeOnset(100)});
    CHECK_FALSE(onset == DetectionEvent{makeOnset(101)});

    const DetectionEvent frame{PitchFrame{
        .input_stream_sample = 100,
        .sample_rate_hz = 48000.0,
        .f0_hz = 82.4,
        .confidence = 0.9F,
        .clarity = 0.8F,
        .rms = 0.25F,
    }};
    CHECK_FALSE(onset == frame);
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
        makeSalience(400),
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

    REQUIRE(events.size() == 4);
    CHECK(inputStreamSampleOf(events[0]) == 100);
    CHECK(inputStreamSampleOf(events[1]) == 200);
    CHECK(inputStreamSampleOf(events[2]) == 300);
    CHECK(inputStreamSampleOf(events[3]) == 400);
    CHECK(std::holds_alternative<OnsetEvent>(events[0]));
    CHECK(std::holds_alternative<PitchFrame>(events[1]));
    CHECK(std::holds_alternative<PitchConfirmation>(events[2]));
    CHECK(std::holds_alternative<PolyphonicSalience>(events[3]));
}

} // namespace rock_hero::game::core
