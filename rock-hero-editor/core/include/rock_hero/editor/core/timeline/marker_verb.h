/*!
\file marker_verb.h
\brief The one table of timeline-marker verb labels the undo history, the log and the UI all read.
*/

#pragma once

#include <cstdint>
#include <string>

namespace rock_hero::editor::core
{

/*!
\brief A timeline-marker verb that has a fixed label.

A verb whose undo entry names the marker it acts on instead (a section's add and delete record its
name) still has a label here when a surface shows the verb itself, as the add prompt's title does.
*/
enum class MarkerVerb : std::uint8_t
{
    /*! \brief Adds a section; the label titles its name prompt. */
    AddSection,

    /*! \brief Renames a section; its undo entry continues the label with both names. */
    RenameSection,

    /*! \brief Moves a section a measure. */
    MoveSection,

    /*! \brief Adds a tempo anchor at a grid slot. */
    AddTempoAnchor,

    /*! \brief Types the tempo of the span an anchor opens; its undo entry adds the tempo. */
    SetTempo,

    /*! \brief Moves a tempo anchor's time. */
    MoveTempoAnchor,

    /*! \brief Deletes a tempo anchor. */
    DeleteTempoAnchor,

    /*! \brief Scales every grid address in the song by a typed factor. */
    ScaleTempo,

    /*! \brief Adds a time signature at a beat, cutting the measure short. */
    AddTimeSignature,

    /*! \brief Restates a time signature, or authors the derived short bar. */
    SetTimeSignature,

    /*! \brief Moves a time signature a beat of the meter before it. */
    MoveTimeSignature,

    /*! \brief Deletes a time signature. */
    DeleteTimeSignature,

    /*! \brief Adds a fret-hand position. */
    AddHandPosition,

    /*! \brief Moves a fret-hand position. */
    MoveHandPosition,

    /*! \brief Sets a fret-hand position's start fret. */
    SetHandPositionStartFret,

    /*! \brief Sets a fret-hand position's end fret. */
    SetHandPositionEndFret,

    /*! \brief Clears a fret-hand position's stated end fret. */
    ClearHandPositionEndFret,

    /*! \brief Deletes a fret-hand position. */
    DeleteHandPosition,

    /*! \brief Removes every stored fret-hand position the derivation does not need. */
    PruneHandPositions,

    /*! \brief States that the hand holds one shape over the selection's extent. */
    HoldShape,

    /*! \brief Moves a hold's start, or a derived span's as it becomes a hold, one note onset. */
    MoveHoldStart,

    /*! \brief Moves a hold's end, or a derived span's as it becomes a hold, one note boundary. */
    MoveHoldEnd,
};

/*!
\brief The verb's label: its undo entry, the label its refusals are logged under, the title of the
prompt it opens and the text of a menu row that runs it directly.
\param verb Verb to name.
\return The label, such as "Add Tempo Anchor".
*/
[[nodiscard]] std::string markerVerbLabel(MarkerVerb verb);

/*!
\brief The text of a menu row that opens the verb's prompt: the label with the trailing ellipsis
that says a prompt follows, the one place that convention is applied.
\param verb Verb to name.
\return The label and "...", such as "Set Tempo...".
*/
[[nodiscard]] std::string markerVerbPromptRow(MarkerVerb verb);

/*!
\brief Scale Tempo's label under a Note Lock state: the one verb takes two forms, and its undo
entry, its prompt's title and its refusals' log line say which.
\param note_lock Whether the Note Lock is on.
\return "Scale Tempo (notes keep their time)" with the lock on, the relabel that holds every time;
        "Scale Tempo (notes follow the beat)" with it off, the stretch that re-times the anchors.
*/
[[nodiscard]] std::string scaleTempoLabel(bool note_lock);

} // namespace rock_hero::editor::core
