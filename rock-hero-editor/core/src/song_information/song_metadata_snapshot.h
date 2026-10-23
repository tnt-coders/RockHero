/*!
\file song_metadata_snapshot.h
\brief The song's whole information snapshot, committed and undone by the shared marker funnel.
*/

#pragma once

#include "controller/edit_focus.h"
#include "controller/marker_judge_context.h"
#include "controller/refusal_light.h"

#include <optional>
#include <rock_hero/common/core/session/session.h>
#include <rock_hero/common/core/song/song.h>

namespace rock_hero::editor::core
{

/*!
\brief The song's descriptive information: title, artist, album and year.

Song-level and timeless: it names the song rather than any place in it, so it rides the marker
funnel for its undo entry, its no-change rule and its refusal path, but no lock governs it and its
landing brings nothing on the timeline into focus. Nothing is required: every field may be left
empty, and a year of 0 means unknown.
*/
struct SongMetadataSnapshot
{
    /*! \brief The song's information. */
    common::core::SongMetadata metadata;

    /*!
    \brief Reads the song's current information.
    \param session Session owning the song.
    \return The information as it now stands.
    */
    [[nodiscard]] static SongMetadataSnapshot capture(const common::core::Session& session);

    /*!
    \brief Writes this information onto the song.

    \param session Session owning the song.
    \return True always; the song always exists, so its information has nowhere else to land.
    */
    [[nodiscard]] bool applyTo(common::core::Session& session) const;

    /*! \brief Strips the spaces and tabs surrounding the title, the artist and the album. */
    void normalize();

    /*!
    \brief Reports the one rule this information can break: a negative year.

    \param context The funnel's context; nothing in it bears on the song's information.
    \return The refusal to report, lighting nothing, or empty when the year is 0 or positive.
    */
    [[nodiscard]] std::optional<PlanRefused> validate(const MarkerJudgeContext& context) const;

    /*!
    \brief What an undo or redo landing on this information brings into focus.
    \param replaced The information the transition replaces.
    \return Nothing, always: the song's information stands at no place on the timeline.
    */
    [[nodiscard]] EditFocus focusReplacing(const SongMetadataSnapshot& replaced) const;

    /*!
    \brief Compares two snapshots by their stored values.
    \param lhs Left-hand snapshot.
    \param rhs Right-hand snapshot.
    \return True when both hold equal information.
    */
    friend bool operator==(const SongMetadataSnapshot& lhs, const SongMetadataSnapshot& rhs) =
        default;
};

} // namespace rock_hero::editor::core
