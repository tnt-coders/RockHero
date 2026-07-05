/*!
\file tone_track.h
\brief Arrangement-owned tone regions that schedule whole-rig tone changes on the song grid.
*/

#pragma once

#include <string>
#include <vector>

namespace rock_hero::common::core
{

/*!
\brief Musical grid position used to place tone regions on the tempo map.

This is intentionally not a note/chart position model. It exists only to address tone-region
endpoints, and it serializes through the same `"<measure>:<beat>"` token grammar as tempo-map
anchors. Extend it if sub-beat snapping becomes necessary rather than introducing broad
chart-position machinery early.
*/
struct ToneGridPosition
{
    /*! \brief One-based measure on the song grid. */
    int measure{1};

    /*! \brief One-based beat within the measure. */
    int beat{1};

    /*!
    \brief Compares two grid positions by their stored fields.
    \param lhs Left-hand grid position.
    \param rhs Right-hand grid position.
    \return True when both positions store equal values.
    */
    friend constexpr bool operator==(
        const ToneGridPosition& lhs, const ToneGridPosition& rhs) noexcept = default;
};

/*! \brief One time-bounded tone region referencing a package tone document. */
struct ToneRegion
{
    /*! \brief Stable region identifier (canonical UUID). */
    std::string id;

    /*! \brief User-facing region name; may be empty, in which case views show a fallback. */
    std::string name;

    /*! \brief Musical start of the region (inclusive). */
    ToneGridPosition start;

    /*! \brief Musical end of the region (exclusive). */
    ToneGridPosition end;

    /*!
    \brief Package-relative tone document interpreted by common/audio.

    Follows the same canonical `tones/<uuid>/tone.json` path rules as the arrangement-level
    tone document reference.
    */
    std::string tone_document_ref;

    /*!
    \brief Compares two tone regions by their stored fields.
    \param lhs Left-hand tone region.
    \param rhs Right-hand tone region.
    \return True when both regions store equal values.
    */
    friend bool operator==(const ToneRegion& lhs, const ToneRegion& rhs) = default;
};

/*!
\brief Authored tone schedule for one arrangement.

Regions are kept sorted by start position and never overlap. Gaps are allowed; playback holds the
previous region's tone through a gap. An empty track means the arrangement's legacy tone document
reference is the sole tone source.
*/
struct ToneTrack
{
    /*! \brief Tone regions in ascending start order. */
    std::vector<ToneRegion> regions;

    /*!
    \brief Compares two tone tracks by their stored regions.
    \param lhs Left-hand tone track.
    \param rhs Right-hand tone track.
    \return True when both tracks store equal regions.
    */
    friend bool operator==(const ToneTrack& lhs, const ToneTrack& rhs) = default;
};

} // namespace rock_hero::common::core
