#include <catch2/catch_test_macros.hpp>
#include <limits>
#include <rock_hero/common/core/timeline/fraction.h>
#include <rock_hero/game/core/scoring/score_math.h>
#include <rock_hero/game/core/scoring/scoring_ruleset.h>

namespace rock_hero::game::core
{

// The GH ladder under rh-score-1: 1x/2x/3x/4x at committed streaks 0/10/20/30, with each
// threshold inclusive and the top rung open-ended. A pre-song negative streak can't exist, but
// the math treats it as zero rather than misbehaving.
TEST_CASE("Multiplier ladder rises at the committed streak thresholds", "[core][scoring]")
{
    const ScoringRuleset ruleset{};
    CHECK(multiplierForStreak(ruleset, 0, false) == 1);
    CHECK(multiplierForStreak(ruleset, 9, false) == 1);
    CHECK(multiplierForStreak(ruleset, 10, false) == 2);
    CHECK(multiplierForStreak(ruleset, 19, false) == 2);
    CHECK(multiplierForStreak(ruleset, 20, false) == 3);
    CHECK(multiplierForStreak(ruleset, 29, false) == 3);
    CHECK(multiplierForStreak(ruleset, 30, false) == 4);
    CHECK(multiplierForStreak(ruleset, 500, false) == 4);
    CHECK(multiplierForStreak(ruleset, -5, false) == 1);
}

// A ruleset with no thresholds is a degenerate but well-defined configuration: the ladder never
// rises (multiplier floors at 1) and the star count floors at zero (nothing to satisfy).
TEST_CASE("Empty ruleset thresholds pin the ladder and star floors", "[core][scoring]")
{
    ScoringRuleset ruleset{};
    ruleset.multiplier_streak_thresholds.clear();
    ruleset.star_ratio_thresholds.clear();
    CHECK(multiplierForStreak(ruleset, 1000, false) == 1);
    CHECK(multiplierForStreak(ruleset, 1000, true) == 2);
    CHECK(starsForScoreRatio(ruleset, 100.0) == 0);
}

// Star power doubles whatever rung the ladder is on, up to the GH-style 8x ceiling.
TEST_CASE("Star power doubles the ladder multiplier", "[core][scoring]")
{
    const ScoringRuleset ruleset{};
    CHECK(multiplierForStreak(ruleset, 0, true) == 2);
    CHECK(multiplierForStreak(ruleset, 10, true) == 4);
    CHECK(multiplierForStreak(ruleset, 30, true) == 8);
}

// Chord base score is the sum of member notes; a single note is a one-member chord.
TEST_CASE("Chord base score sums the member notes", "[core][scoring]")
{
    const ScoringRuleset ruleset{};
    CHECK(baseScoreForChord(ruleset, 1) == 50);
    CHECK(baseScoreForChord(ruleset, 3) == 150);
    CHECK(baseScoreForChord(ruleset, 6) == 300);
}

// Sustain credit is 25 per beat pro-rated by the held fraction, rounded to the nearest point,
// with tracker noise past a full hold clamped instead of minting extra score.
TEST_CASE("Sustain credit pro-rates by the held fraction", "[core][scoring]")
{
    const ScoringRuleset ruleset{};
    CHECK(sustainScoreForHold(ruleset, common::core::Fraction{2, 1}, 1.0) == 50);
    CHECK(sustainScoreForHold(ruleset, common::core::Fraction{2, 1}, 0.5) == 25);
    CHECK(sustainScoreForHold(ruleset, common::core::Fraction{1, 2}, 1.0) == 13);
    CHECK(sustainScoreForHold(ruleset, common::core::Fraction{}, 1.0) == 0);
    CHECK(sustainScoreForHold(ruleset, common::core::Fraction{2, 1}, 0.0) == 0);
    CHECK(sustainScoreForHold(ruleset, common::core::Fraction{2, 1}, 1.5) == 50);
    CHECK(sustainScoreForHold(ruleset, common::core::Fraction{2, 1}, -0.5) == 0);
}

// A degenerate sustain tracker can emit 0/0: a non-finite held fraction earns nothing instead
// of feeding lround an unspecified value (NaN passes std::clamp untouched).
TEST_CASE("Non-finite held fractions earn no sustain credit", "[core][scoring]")
{
    const ScoringRuleset ruleset{};
    CHECK(
        sustainScoreForHold(
            ruleset, common::core::Fraction{2, 1}, std::numeric_limits<double>::quiet_NaN()) == 0);
    CHECK(
        sustainScoreForHold(
            ruleset, common::core::Fraction{2, 1}, std::numeric_limits<double>::infinity()) == 0);
}

// Star awards under rh-score-1: 1/2/3/4/5 stars at score-to-max-base ratios 0.2/0.6/1.2/2.0/2.8,
// thresholds inclusive, with 0 stars below the lowest cutoff — the no-fail floor. A completed
// no-fail run that plays at a failing level can land on 0 or 1 star; a degenerate negative or
// NaN ratio satisfies nothing and scores 0.
TEST_CASE("Star award counts the satisfied ratio thresholds", "[core][scoring]")
{
    const ScoringRuleset ruleset{};
    CHECK(starsForScoreRatio(ruleset, 0.0) == 0);
    CHECK(starsForScoreRatio(ruleset, 0.19) == 0);
    CHECK(starsForScoreRatio(ruleset, 0.2) == 1);
    CHECK(starsForScoreRatio(ruleset, 0.5) == 1);
    CHECK(starsForScoreRatio(ruleset, 0.59) == 1);
    CHECK(starsForScoreRatio(ruleset, 0.6) == 2);
    CHECK(starsForScoreRatio(ruleset, 1.2) == 3);
    CHECK(starsForScoreRatio(ruleset, 2.0) == 4);
    CHECK(starsForScoreRatio(ruleset, 2.8) == 5);
    CHECK(starsForScoreRatio(ruleset, 4.0) == 5);
    CHECK(starsForScoreRatio(ruleset, -1.0) == 0);
    CHECK(starsForScoreRatio(ruleset, std::numeric_limits<double>::quiet_NaN()) == 0);
}

// The default-constructed ruleset IS rh-score-1: the version string is part of the covenant
// that any constant change bumps the version, so it is pinned like the constants themselves.
TEST_CASE("Default ruleset carries the rh-score-1 version", "[core][scoring]")
{
    CHECK(ScoringRuleset{}.version == "rh-score-1");
}

} // namespace rock_hero::game::core
