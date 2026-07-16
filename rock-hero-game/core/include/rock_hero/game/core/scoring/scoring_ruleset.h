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
`ScoringRuleset{}` to score under the current rules.
*/
struct ScoringRuleset
{
    /*! \brief Identity of this constant set; bumped whenever any constant changes. */
    std::string version{"rh-score-1"};

    /*! \brief Half-width of the onset hit window in real milliseconds around the expected time. */
    double onset_window_half_width_ms{100.0};

    /*!
    \brief Ascending committed-streak thresholds that raise the multiplier ladder.

    The multiplier is 1 plus the number of satisfied thresholds, so `{10, 20, 30}` is the
    GH-style 1x/2x/3x/4x ladder.
    */
    std::vector<int> multiplier_streak_thresholds{10, 20, 30};

    /*! \brief Factor applied to the ladder multiplier while star power is active. */
    int star_power_multiplier_factor{2};

    /*! \brief Base score of one hit note; a chord scores the sum of its member notes. */
    int base_note_score{50};

    /*! \brief Sustain score per beat of sustain, pro-rated by the held fraction. */
    int sustain_score_per_beat{25};

    /*!
    \brief Ascending score-to-max-base-score ratio thresholds that award stars.

    Stars are 1 plus the number of satisfied thresholds, so `{0.6, 1.2, 2.0, 2.8}` awards
    2 stars at a 0.6 ratio up through 5 stars at 2.8.
    */
    std::vector<double> star_ratio_thresholds{0.6, 1.2, 2.0, 2.8};
};

} // namespace rock_hero::game::core
