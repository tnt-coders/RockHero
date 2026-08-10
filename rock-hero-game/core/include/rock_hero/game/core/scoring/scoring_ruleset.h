/*!
\file scoring_ruleset.h
\brief The versioned constants every scoring decision reads (plan 24 scoring contract).
*/

#pragma once

#include <string>
#include <vector>

namespace rock_hero::game::core
{

/*!
\brief One versioned set of scoring tunables.

All hit windows, ladder thresholds, and score constants live here so a score is meaningful only
together with the ruleset that produced it: every score record carries the version string, and
ANY constant change bumps the version — records are self-describing, so old records stay honest
and comparable per version. The member initializers ARE ruleset `rh-score-1`; construct with
`ScoringRuleset{}` to score under the current rules. These constants track the signed feel baseline
(plan 24 §1); the ones carried over from earlier-generation variants are proxies, recorded as such
in the plan.
*/
struct ScoringRuleset
{
    /*! \brief Identity of this constant set; bumped whenever any constant changes. */
    std::string version{"rh-score-1"};

    /*!
    \brief Half-width of the onset hit window in real milliseconds around the expected time.

    ±100 ms is the widest window the genre documents, kept at v1 as margin for detection timing
    jitter. The signed feel baseline is attested tighter (no published figure of its own; Clone
    Hero's 140 ms total is the closest published equivalent), so the recorded tuning direction is
    toward ~±70 ms by ruleset version once plan 23 measures real jitter.
    */
    double onset_window_half_width_ms{100.0};

    /*!
    \brief Ascending committed-streak thresholds that raise the multiplier ladder.

    The multiplier is 1 plus the number of satisfied thresholds, so `{10, 20, 30}` is the
    genre-standard 1x/2x/3x/4x ladder.
    */
    std::vector<int> multiplier_streak_thresholds{10, 20, 30};

    /*! \brief Factor applied to the ladder multiplier while star power is active. */
    int star_power_multiplier_factor{2};

    /*!
    \brief Base score of one hit note; a chord scores the sum of its member notes.

    A two-note chord banks 100, a three-note chord 150 — chord risk is rewarded in score while
    staying one unit for streak, multiplier, and meter purposes.
    */
    int base_note_score{50};

    /*! \brief Sustain score per beat of sustain, pro-rated by the held fraction. */
    int sustain_score_per_beat{25};

    /*!
    \brief Cents tolerance around the charted pitch trajectory that counts a sustain as held.

    The reference is the trajectory, not the base pitch: base pitch plus interpolated bend
    curve and slide waypoints, with vibrato excursions allowed — a correctly executed bend must
    never dock its own sustain credit.
    */
    double sustain_tolerance_cents{100.0};

    /*!
    \brief Whether a qualifying unmatched onset breaks the committed streak (overstrum penalty).

    Deliberately one flag to walk back: flipping this to false (with a version bump) yields
    no-penalty play without touching the state machine. An overstrum never marks any
    chart note missed and never counts against accuracy — it only resets streak and applies the
    miss-sized meter delta.
    */
    bool overstrum_breaks_streak{true};

    /*!
    \brief Minimum onset strength for an unmatched Transient onset to qualify as an overstrum.

    The noise gate that keeps string scrapes and handling noise from killing streaks: only
    deliberate-strum-strength onsets qualify. Tuned against plan 23's noise-floor fixtures
    before the penalty is trusted; PitchStep-origin and strum-coalesced onsets never qualify.
    */
    double overstrum_strength_threshold{0.5};

    /*!
    \brief Minimum pitch-frame confidence that lets a deadline lapse confirm a pitched note.

    The anti-mash guard: confirm-by-default requires at least one in-span pitch frame at or
    above this confidence whose f0 is octave-insensitively consistent with the expected pitch.
    Full-mute-charted notes are exempt; an evidence-free lapse resolves MissNoPitchEvidence.
    */
    double lapse_evidence_min_confidence{0.1};

    /*!
    \brief Ascending score-to-max-base-score ratio thresholds that award stars.

    Stars are the count of satisfied thresholds, so `{0.2, 0.6, 1.2, 2.0, 2.8}` awards 0 stars
    below 0.2, then 1/2/3/4/5 stars at 0.2/0.6/1.2/2.0/2.8. The lowest cutoff makes 0 and 1 stars
    reachable — in practice only in no-fail mode, since a fail-enabled run that would score that
    low fails out before it can complete (plan 24 §6). A failed, incomplete run records 0 stars
    by override at finalization, independent of these cutoffs. All five values are proxies for the
    signed feel baseline (only the upper cutoffs have documented genre precedent; the 0.2 floor is
    RockHero's, pending plan-23 tuning). Ratio stars cap at 5: a strict full combo (every note hit,
    zero qualifying overstrums) is a separate predicate the state machine records as `fullCombo`,
    and presentation renders it as 5 GOLD stars — never a sixth star, and never decided by a ratio.
    */
    std::vector<double> star_ratio_thresholds{0.2, 0.6, 1.2, 2.0, 2.8};
};

} // namespace rock_hero::game::core
