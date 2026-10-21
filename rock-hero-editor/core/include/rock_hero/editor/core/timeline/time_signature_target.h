/*!
\file time_signature_target.h
\brief The meter verbs a beat offers: restate or author the reign starting there, insert a time
signature at that beat, or nothing.
*/

#pragma once

#include <rock_hero/common/core/timeline/grid_position.h>
#include <rock_hero/common/core/timeline/tempo_map.h>
#include <string>
#include <variant>

namespace rock_hero::editor::core
{

/*!
\brief The verb that states the meter of the reign starting at a position: the meter prompt,
opened on the meter the chip prints.

On an authored change the typed meter restates it; on a derived short bar it AUTHORS it — a stored
change is inserted at the bar's place — as a stating verb on a derived fret-hand position authors
the position first. The prompt the time-signature chord and `Enter` ask for (`MarkerPrompt`); its
typed text goes back through `IEditorController::onTimeSignatureSetRequested`.
*/
struct SetTimeSignatureTarget
{
    /*! \brief The reign's first downbeat. */
    common::core::GridPosition position{};

    /*! \brief The meter as its chip prints it ("7/8"), the prompt's starting text. */
    std::string meter_text{};

    /*!
    \brief Compares two targets field by field.
    \return True when both name the same reign under the same printed meter.
    */
    bool operator==(const SetTimeSignatureTarget&) const = default;
};

/*!
\brief The verb that inserts a time signature at a beat where no reign starts and a marker may
stand: the meter prompt, opened on the meter governing that beat now.

The typed text goes back through the same `IEditorController::onTimeSignatureSetRequested` the
restate uses, since both put a meter at a position.
*/
struct InsertTimeSignatureTarget
{
    /*! \brief The beat the new time signature will start. */
    common::core::GridPosition position{};

    /*! \brief The meter governing that beat now, the prompt's starting text. */
    std::string meter_text{};

    /*!
    \brief Compares two targets field by field.
    \return True when both name the same beat under the same printed meter.
    */
    bool operator==(const InsertTimeSignatureTarget&) const = default;
};

/*!
\brief What a meter verb at a beat would do: state the reign starting there, insert a time
signature there, or nothing.

For `Ctrl+/` it is read at the beat the cursor is in, never from the selection; nothing is the end
of the song, where no meter may start.
*/
using TimeSignatureTarget =
    std::variant<std::monostate, SetTimeSignatureTarget, InsertTimeSignatureTarget>;

/*!
\brief The one rule for what a meter verb does at a beat, shared by `Ctrl+/` and the ruler's
signature menu.

The reign governing the beat is read once: where it starts on this very beat — authored or
derived — it is stated, opened on its own meter; elsewhere one is inserted at the beat, opened on
that governing meter, where a marker may start (not at the terminal anchor or past it); otherwise
nothing. A meter inserted mid-measure cuts the measure short, and the cut becomes the derived
short bar.

\param tempo_map Map the beat is read under.
\param beat The beat, with no sub-beat offset (\ref common::core::beatStart).
\return The restate-or-author, the insert, or nothing.
*/
[[nodiscard]] TimeSignatureTarget timeSignatureTargetAt(
    const common::core::TempoMap& tempo_map, common::core::GridPosition beat);

} // namespace rock_hero::editor::core
