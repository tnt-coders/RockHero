/*!
\file tempo_map_edits.h
\brief Pure tempo-map edits behind the tempo anchor's marker verbs.
*/

#pragma once

#include <cstdint>
#include <expected>
#include <rock_hero/common/core/timeline/tempo_map.h>
#include <string>

namespace rock_hero::editor::core
{

/*! \brief Stable reasons a tempo-map edit refuses. */
enum class TempoMapEditErrorCode : std::uint8_t
{
    /*! \brief No anchor stands at the named beat, or none that opens a span there. */
    NoAnchorAtBeat,

    /*! \brief A delete named the first or the terminal anchor, which every map must keep. */
    ProtectedAnchor,

    /*! \brief A typed tempo is not a positive finite number. */
    InvalidBpm,

    /*! \brief The edited map breaks a structural tempo-map rule (anchors would cross or touch). */
    BrokenRule,
};

/*! \brief Recoverable failure produced by a tempo-map edit. */
struct [[nodiscard]] TempoMapEditError
{
    /*! \brief Stable error code used by callers for branching. */
    TempoMapEditErrorCode code{};

    /*! \brief Human-readable diagnostic for the refusal log. */
    std::string message;
};

/*! \brief A tempo map produced by an edit, or why the edit refused. */
using TempoMapEditResult = std::expected<common::core::TempoMap, TempoMapEditError>;

/*!
\brief Inserts an anchor at a beat, pinning the time the map already gives that beat.

The anchor's time is the beat's current time rounded to a whole millisecond, so the insert moves
nothing by more than half a millisecond.

\param tempo_map Map to edit.
\param measure One-based measure of the beat.
\param beat One-based beat within the measure.
\return The edited map, or the refusal.
*/
[[nodiscard]] TempoMapEditResult insertTempoAnchor(
    const common::core::TempoMap& tempo_map, int measure, int beat);

/*!
\brief Moves an anchor to a new time, every other anchor staying where it is.

The two spans beside the anchor change tempo; nothing else does. A time that would reach a
neighbour, or take the first anchor below zero, is refused rather than clamped.

\param tempo_map Map to edit.
\param measure One-based measure of the anchor.
\param beat One-based beat of the anchor.
\param seconds New time, rounded to a whole millisecond.
\return The edited map, or the refusal.
*/
[[nodiscard]] TempoMapEditResult moveTempoAnchor(
    const common::core::TempoMap& tempo_map, int measure, int beat, double seconds);

/*!
\brief Sets the quarter-note tempo of the span an anchor opens, rippling every later anchor.

The anchor stays where it is. The span's end moves to the time the typed tempo gives it, and every
later anchor shifts by the same whole number of milliseconds, so every later span keeps its tempo.
When the shifted terminal anchor falls before \p cover_until_seconds, it grows to the first
downbeat at or past that time at the last span's tempo; it never shrinks.

\param tempo_map Map to edit.
\param measure One-based measure of the anchor opening the span.
\param beat One-based beat of the anchor opening the span.
\param quarter_bpm Typed tempo in quarter notes per minute.
\param cover_until_seconds Time the map must still reach, the end of the song's audio.
\return The edited map, or the refusal.
*/
[[nodiscard]] TempoMapEditResult setTempoSpanBpm(
    const common::core::TempoMap& tempo_map, int measure, int beat, double quarter_bpm,
    double cover_until_seconds);

/*!
\brief Deletes an anchor, its neighbours staying where they are.

The exact inverse of \ref insertTempoAnchor: the two spans beside it merge into one.

\param tempo_map Map to edit.
\param measure One-based measure of the anchor.
\param beat One-based beat of the anchor.
\return The edited map, or the refusal.
*/
[[nodiscard]] TempoMapEditResult deleteTempoAnchor(
    const common::core::TempoMap& tempo_map, int measure, int beat);

/*!
\brief Returns the time one keyboard nudge moves an anchor: one on-screen pixel.

Zoom is the precision control, so a nudge is the smallest change the charter can see, and never
less than one millisecond, the finest time an anchor can store.

\param pixels_per_second Current timeline zoom; zero or less when the view has not reported one.
\return Nudge size in whole milliseconds, at least one.
*/
[[nodiscard]] std::int64_t tempoAnchorNudgeMilliseconds(double pixels_per_second) noexcept;

} // namespace rock_hero::editor::core
