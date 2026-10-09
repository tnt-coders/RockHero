/*!
\file time_signature_target.h
\brief The meter verbs a measure offers: restate the time signature standing on its downbeat,
insert one there, or nothing.
*/

#pragma once

#include <rock_hero/common/core/timeline/tempo_map.h>
#include <string>
#include <variant>

namespace rock_hero::editor::core
{

/*!
\brief The verb that restates the time signature standing at a measure: the meter prompt, opened
on the meter the chip prints.

Published as the time-signature chord's or the restate verb's answer
(\ref EditorViewState::time_signature_chord_target, \ref EditorViewState::restate_target); the
prompt's typed text goes back through `IEditorController::onTimeSignatureSetRequested`.
*/
struct SetTimeSignatureTarget
{
    /*! \brief Measure the time signature starts. */
    int measure{1};

    /*! \brief The meter as its chip prints it ("7/8"), the prompt's starting text. */
    std::string meter_text{};

    /*!
    \brief Compares two targets field by field.
    \return True when both name the same time signature under the same printed meter.
    */
    bool operator==(const SetTimeSignatureTarget&) const = default;
};

/*!
\brief The verb that inserts a time signature at a measure downbeat found free and able to carry
one: the meter prompt, opened on the meter governing that measure now.

The typed text goes back through the same `IEditorController::onTimeSignatureSetRequested` the
restate uses, since both put a meter at a measure's downbeat.
*/
struct InsertTimeSignatureTarget
{
    /*! \brief Measure whose downbeat the new time signature will start. */
    int measure{1};

    /*! \brief The meter governing that measure now, the prompt's starting text. */
    std::string meter_text{};

    /*!
    \brief Compares two targets field by field.
    \return True when both name the same measure under the same printed meter.
    */
    bool operator==(const InsertTimeSignatureTarget&) const = default;
};

/*!
\brief What a meter verb at a measure would do: restate the time signature standing on its
downbeat, insert one there, or nothing.

The meter twin of `TempoChordTarget`. As `Ctrl+/`'s answer it is read at the measure the cursor
is in, never from the selection, and it is nothing while the transport plays or with no song.
*/
using TimeSignatureChordTarget =
    std::variant<std::monostate, SetTimeSignatureTarget, InsertTimeSignatureTarget>;

/*!
\brief The one rule for what a meter verb does at a measure, shared by `Ctrl+/` and the ruler's
signature menu.

The time signature governing the measure is read once: where it starts on this measure's downbeat
it is restated, opened on its own meter; elsewhere one is inserted, opened on that governing meter,
where a marker may start (not at the terminal anchor or past it); otherwise nothing.

\param tempo_map Map the measure is read under.
\param measure One-based measure.
\return The restate, the insert, or nothing.
*/
[[nodiscard]] TimeSignatureChordTarget timeSignatureTargetAt(
    const common::core::TempoMap& tempo_map, int measure);

} // namespace rock_hero::editor::core
