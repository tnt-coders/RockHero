/*!
\file score_math.h
\brief Pure ruleset arithmetic: multiplier ladder, base scores, sustain credit, and stars.
*/

#pragma once

#include <rock_hero/common/core/timeline/fraction.h>
#include <rock_hero/game/core/scoring/scoring_ruleset.h>

namespace rock_hero::game::core
{

/*!
\brief Resolves the score multiplier for a committed streak.

The ladder is computed only over the committed ledger, never over provisional hits. Displayed
counters render committed state and never roll back — a revocation surfaces as a late miss cue
on the gem, and no banked note is ever retroactively mis-scored.

\param ruleset Constants defining the ladder thresholds and the star-power factor.
\param committed_streak Consecutive committed hits entering the note being scored (the streak
       BEFORE this note commits): the note whose commit makes the streak 10 still scores at 1x,
       and the next one earns 2x — the GH fencepost. Non-negative.
\param star_power_active True while a star-power deployment is draining.
\return Ladder multiplier, doubled by the star-power factor when active.
*/
[[nodiscard]] int multiplierForStreak(
    const ScoringRuleset& ruleset, int committed_streak, bool star_power_active) noexcept;

/*!
\brief Resolves the base score of one chord verdict.

A single note is a one-member chord; a chord scores the sum of its member notes.

\param ruleset Constants defining the per-note base score.
\param member_note_count Number of simultaneous chart notes in the chord; positive.
\return Base score before any multiplier.
*/
[[nodiscard]] int baseScoreForChord(const ScoringRuleset& ruleset, int member_note_count) noexcept;

/*!
\brief Resolves the sustain credit for a held note.

Credit is pro-rated by the held fraction and rounded to the nearest point; a dropped sustain
banks the partial credit earned before the drop.

\param ruleset Constants defining the per-beat sustain score.
\param sustain_beats Charted sustain length in beats; zero means no sustain and no credit.
       Never negative — chart validation rejects negative sustains upstream.
\param held_fraction Fraction of the sustain span held in tolerance; clamped to [0, 1], and a
       non-finite value earns nothing.
\return Sustain score before any multiplier.
*/
[[nodiscard]] int sustainScoreForHold(
    const ScoringRuleset& ruleset, common::core::Fraction sustain_beats,
    double held_fraction) noexcept;

/*!
\brief Resolves the ratio-based star award for a completed run.

This is the whole 1–5 star scale. A strict full combo (every note hit, zero qualifying
overstrums) is a separate machine-recorded predicate rendered as 5 GOLD stars by presentation —
it never changes the count this function returns.

\param ruleset Constants defining the ascending ratio thresholds.
\param score_to_max_base_ratio Final score divided by the chart's maximum base (unmultiplied)
       score.
\return Stars from 1 (below every threshold) up to one more than the threshold count.
*/
[[nodiscard]] int starsForScoreRatio(
    const ScoringRuleset& ruleset, double score_to_max_base_ratio) noexcept;

} // namespace rock_hero::game::core
