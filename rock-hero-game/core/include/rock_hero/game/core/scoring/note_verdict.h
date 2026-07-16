/*!
\file note_verdict.h
\brief The per-note scoring outcome vocabulary (plan 24 scoring contract).
*/

#pragma once

#include <optional>

namespace rock_hero::game::core
{

/*! \brief Terminal outcome of one chart note after the provisional-hit machine commits it. */
enum class NoteVerdictCode
{
    /*! \brief Onset matched and pitch evidence confirmed the note. */
    Hit,

    /*! \brief Onset matched and the confirmation deadline lapsed without decisive pitch. */
    HitOnsetOnly,

    /*! \brief The hit window closed without any matching onset. */
    MissNoOnset,

    /*! \brief A provisional hit was revoked by confident contradicting pitch evidence. */
    MissWrongPitch
};

/*!
\brief One committed note verdict with its supporting detection evidence.

Verdicts are what the committed ledger stores and what the score record serializes per note.
Evidence fields are optional because not every outcome produces them: a MissNoOnset has no
timing delta, and an onset-only hit has no detected pitch.
*/
struct NoteVerdict
{
    /*! \brief Terminal outcome of the note. */
    NoteVerdictCode code{NoteVerdictCode::MissNoOnset};

    /*!
    \brief Signed onset timing error in real milliseconds; negative means early.

    Empty when no onset matched the note. Recorded signed so the results screen can show
    early/late tendency.
    */
    std::optional<double> timing_delta_ms;

    /*!
    \brief Detected sounding pitch in absolute cents (100 times the MIDI note number).

    A4 = 6900. Empty when no pitch evidence resolved the note (missed notes, onset-only hits,
    and percussive full-mute notes).
    */
    std::optional<double> detected_pitch_cents;

    /*! \brief Confidence of the resolving pitch evidence in [0, 1]; empty without evidence. */
    std::optional<float> confidence;

    /*! \brief Fraction of the note's sustain span held in tolerance; zero for unsustained notes. */
    double sustain_held_fraction{0.0};
};

} // namespace rock_hero::game::core
