/*!
\file tone_track.h
\brief Arrangement-owned tone regions that schedule whole-rig tone changes on the song grid.
*/

#pragma once

#include <rock_hero/common/core/chart/chart.h>
#include <string>
#include <vector>

namespace rock_hero::common::core
{

// Tone-region endpoints address the same measure/beat/sub-beat grid as chart notes and parameter
// automation points, so they reuse the shared GridPosition type (chart/chart.h) and its
// `"<measure>:<beat>"` / `"<measure>:<beat>+<n>/<d>"` token grammar. This keeps tone-region
// snapping consistent with the automation lanes rendered directly beneath the tone row.

/*!
\brief A named, reusable tone: a signal chain that the arrangement's tone catalog owns.

The name lives on the tone rather than on each region, so reusing a tone (two regions referencing
the same document) reads with one consistent name and renaming updates every occurrence at once. The
signal chain itself is the opaque, audio-owned document at tone_document_ref, which is also the
tone's stable identity within an arrangement.
*/
struct Tone
{
    /*!
    \brief Package-relative tone document (`tones/<uuid>/tone.json`); the tone's stable identity.
    */
    std::string tone_document_ref;

    /*! \brief User-facing tone name shown on regions and in the tone picker. */
    std::string name;

    /*!
    \brief Compares two tones by their stored fields.
    \param lhs Left-hand tone.
    \param rhs Right-hand tone.
    \return True when both tones store equal values.
    */
    friend bool operator==(const Tone& lhs, const Tone& rhs) = default;
};

/*!
\brief One tone change: the region it opens runs from its start to the next region's start.

A region stores only where it BEGINS. Its end is the next region's start (the tempo map's terminal
anchor for the last), so the track tiles the song with no gap and no overlap by construction, and
the persisted form (`toneChanges`) is exactly this list. The id is session-scoped: minted when a
package loads and never persisted.
*/
struct ToneRegion
{
    /*! \brief Session-scoped region identifier (canonical UUID). */
    std::string id;

    /*! \brief Musical start of the region (inclusive); the tone change itself. */
    GridPosition start;

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

Regions are kept in strictly ascending start order, the first starting at the song's first
downbeat, so they tile the whole song. A region boundary IS a tone change: consecutive regions
never reference the same tone, because a boundary that changed nothing would be no boundary at all
(\ref coalesceToneRegions in tone_track_edits.h is the one place that law is applied, by every
edit and by the package reader). An empty track means no scheduled tone regions; load normalization
mints a default tone plus a whole-song region before the editor sees the arrangement.
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
