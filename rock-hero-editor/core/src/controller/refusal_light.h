/*!
\file refusal_light.h
\brief What a refused edit lights: one set of targets across every lane, and the refusal that
carries it.
*/

#pragma once

#include "chart/chart_selection.h"
#include "controller/editor_selection.h"

#include <rock_hero/common/core/chart/chart.h>
#include <string>
#include <variant>
#include <vector>

namespace rock_hero::editor::core
{

/*!
\brief One thing a refusal flash can light, named exactly as the selection names it.

The selection's own identity types rather than a second key set, so a refusal and a selection name
a note, a keyframe or a marker the one way: by its slot, its hold's or marker's start, its meter's
first measure or its tone region's id — the chart's and the song's identity axis, never a
projection index or a time in seconds, because a refusal is decided where no projection exists.
*/
using RefusalTarget = std::variant<
    ChartNoteKey, ChartKeyframeKey, SpanSelection, NoneHoldSelection, FretHandPositionSelection,
    SongSectionSelection, TempoAnchorSelection, TimeSignatureSelection, ToneRegionSelection>;

/*!
\brief What a refusal lights: a SET of targets, of any kinds, which the view glows as one.

A refusal lights the thing the edit tried to change and the thing that stood in its way. Each
refusing site adds what it knows — the culprit a rule names, the hold a push would empty — and the
controller adds the subject where it holds it, so neither has to know what the other added; the
union is what flashes, and the painter never tells the two apart. A target the current projection
cannot resolve (a note the refused plan would have created) lights nothing, which is right: there
is no mark for it yet.
*/
struct RefusalLight
{
    /*! \brief The targets, each once, in the order they were first added. */
    std::vector<RefusalTarget> targets{};

    /*!
    \brief Adds one target, unless the light already holds an equal one.
    \param target Target to light.
    */
    void add(RefusalTarget target);

    /*!
    \brief Adds every target another light holds: the union of the two.
    \param light Light to merge in.
    */
    void add(const RefusalLight& light);

    /*!
    \brief Reports whether the light names nothing.
    \return True when no target was added.
    */
    [[nodiscard]] bool empty() const noexcept;
};

/*!
\brief The light of a chart selection: every object it holds, of either kind.
\param selection Selection to light.
\return A light naming each selected note and keyframe.
*/
[[nodiscard]] RefusalLight lightOf(const ChartSelection& selection);

/*!
\brief The light of a marker selection: the one marker it names.
\param selection Selection to light.
\return A light naming that marker.
*/
[[nodiscard]] RefusalLight lightOf(const MarkerSelection& selection);

/*!
\brief The target a hold is lit as: the span an unnamed hold states, or the none hold itself.

Both named by the hold's start, as the selection names them, so a hold standing in a refused edit's
way lights its own mark on the span rows rather than the notes under it.

\param hold Hold to light.
\return The hold's target.
*/
[[nodiscard]] RefusalTarget holdTarget(const common::core::ShapeHold& hold);

/*! \brief The edit would change nothing a document records — legal, just empty. */
struct PlanNoChange
{
};

/*!
\brief The result would break a rule or a planner's own bound, so the whole plan is refused, never
clamped: why, and what the refusing site lights.

`lit` holds what the refusing site knows: where another authored thing stands in the way, that is
the culprit — the note a move would land on, the ring whose statement an edit would cut off, the
neighbour hold a push would empty, the stretch a hold runs on past — and where nothing does, the
refused elements themselves. The light is stated at every refusing site, never defaulted: a site
that can light nothing (an empty slot, no chart) says so with an empty light. The controller adds
the subject it planned over before the flash (\ref RefusalLight).
*/
struct PlanRefused
{
    /*! \brief Why, in words for the log: the rule's own message, or the planner's bound. */
    std::string reason;

    /*! \brief What the refusing site lights. */
    RefusalLight lit;
};

/*!
\brief Why a planner returned no plan: a valid no-op is not a refusal.

The two emptinesses are kept apart because sharing one `std::nullopt` makes every refusal in the
editor silent — no caller can tell "this edit is not allowed" from "this edit changes nothing", so
nothing can report the former without lying about the latter. The pending fret entry is the
consumer that needs the split: a provisional value that plans to a no-op is VALID and must not
paint red. A sum rather than a kind beside a string, so only a refusal can carry a reason; the
reason goes to the log, and what the screen shows is the refusal flash.
*/
using PlanRefusal = std::variant<PlanNoChange, PlanRefused>;

} // namespace rock_hero::editor::core
