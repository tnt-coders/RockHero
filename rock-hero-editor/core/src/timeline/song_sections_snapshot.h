/*!
\file song_sections_snapshot.h
\brief Whole song-section list snapshot committed and undone by the shared marker commit funnel.
*/

#pragma once

#include <optional>
#include <rock_hero/common/core/session/session.h>
#include <rock_hero/common/core/song/song.h>
#include <string>
#include <vector>

namespace rock_hero::editor::core
{

/*!
\brief The song's whole section list.

Song-level rather than per-arrangement: sections describe the song's structure, not one
arrangement's tab. A section is two fields and a song carries tens of them, so the whole list is
cheaper to carry than a diff would be to compute.

\note A section edit never re-runs the fret-hand phrase-boundary generator. That generator reads
      section starts as phrase boundaries at IMPORT only, producing stored FretHandPosition
      records; re-running it on an authored edit would silently overwrite hand positions the
      charter placed. Sections are navigation structure — moving one moves no hand.
*/
struct SongSectionsSnapshot
{
    /*! \brief The song's section markers. */
    std::vector<common::core::SongSection> sections;

    /*!
    \brief Reads the song's current section list.
    \param session Session owning the song.
    \return The list as it now stands.
    */
    [[nodiscard]] static SongSectionsSnapshot capture(const common::core::Session& session);

    /*!
    \brief Writes this list onto the song.

    \param session Session owning the song.
    \return True always; the song always exists, so a section list has nowhere else to land.
    */
    [[nodiscard]] bool applyTo(common::core::Session& session) const;

    /*! \brief Restores the strictly-ascending position order every consumer reads the list in. */
    void normalize();

    /*!
    \brief Reports the first structural rule this list breaks.

    \param session Session supplying the tempo map the section positions address.
    \return The violation to report, or empty when the list satisfies every rule.
    */
    [[nodiscard]] std::optional<std::string> validate(const common::core::Session& session) const;

    /*!
    \brief Compares two snapshots by their stored values.
    \param lhs Left-hand snapshot.
    \param rhs Right-hand snapshot.
    \return True when both hold equal section lists.
    */
    friend bool operator==(const SongSectionsSnapshot& lhs, const SongSectionsSnapshot& rhs) =
        default;
};

} // namespace rock_hero::editor::core
