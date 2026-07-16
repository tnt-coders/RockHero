#include <algorithm>
#include <cmath>
#include <rock_hero/common/core/timeline/fraction.h>
#include <rock_hero/game/core/scoring/score_math.h>
#include <rock_hero/game/core/scoring/scoring_ruleset.h>

namespace rock_hero::game::core
{

// The ladder is 1 plus the number of satisfied ascending thresholds; star power multiplies the
// ladder result rather than adding a rung, so a future ladder change composes with it unchanged.
int multiplierForStreak(const ScoringRuleset& ruleset, int committed_streak, bool star_power_active)
{
    int satisfied = 0;
    for (const int threshold : ruleset.multiplier_streak_thresholds)
    {
        if (committed_streak >= threshold)
        {
            ++satisfied;
        }
    }
    const int ladder_multiplier = 1 + satisfied;
    return star_power_active ? ladder_multiplier * ruleset.star_power_multiplier_factor
                             : ladder_multiplier;
}

// A chord is scored as the sum of its member notes; a single note is a one-member chord.
int baseScoreForChord(const ScoringRuleset& ruleset, int member_note_count)
{
    return ruleset.base_note_score * member_note_count;
}

// Pro-rated credit rounds to the nearest point so partial holds bank deterministic integers;
// the clamp keeps tracker noise (a held fraction slightly past 1) from minting extra score.
int sustainScoreForHold(
    const ScoringRuleset& ruleset, common::core::Fraction sustain_beats, double held_fraction)
{
    const double clamped_fraction = std::clamp(held_fraction, 0.0, 1.0);
    const double credit = static_cast<double>(ruleset.sustain_score_per_beat) *
                          sustain_beats.toDouble() * clamped_fraction;
    return static_cast<int>(std::lround(credit));
}

// Stars are 1 plus the number of satisfied ascending ratio thresholds.
int starsForScoreRatio(const ScoringRuleset& ruleset, double score_to_max_base_ratio)
{
    int satisfied = 0;
    for (const double threshold : ruleset.star_ratio_thresholds)
    {
        if (score_to_max_base_ratio >= threshold)
        {
            ++satisfied;
        }
    }
    return 1 + satisfied;
}

} // namespace rock_hero::game::core
