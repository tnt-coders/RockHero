/*!
\file tone_track_view_state.h
\brief Framework-free render state for the editor's tone track row.
*/

#pragma once

#include <rock_hero/common/core/timeline/timeline.h>
#include <rock_hero/common/core/tone/tone_track.h>
#include <string>
#include <vector>

namespace rock_hero::editor::core
{

/*! \brief One tone region rendered on the tone track row. */
struct ToneRegionViewState
{
    /*! \brief Stable region identifier used by future selection intents. */
    std::string id;

    /*! \brief User-facing region name; empty means the view shows a fallback label. */
    std::string name;

    /*! \brief Musical start of the region (inclusive). */
    common::core::ToneGridPosition grid_start;

    /*! \brief Musical end of the region (exclusive). */
    common::core::ToneGridPosition grid_end;

    /*! \brief Region span in song seconds, resolved through the tempo map. */
    common::core::TimeRange time_range;

    /*!
    \brief True for the runtime-only region synthesized from the legacy tone document.

    A legacy arrangement without an authored tone track displays one full-length default region;
    the view renders it as a read-only continuation rather than authored content.
    */
    bool synthesized_default{false};

    /*! \brief True when this region is the current tone selection. */
    bool selected{false};

    /*!
    \brief Compares two region view states by their stored values.
    \param lhs Left-hand region view state.
    \param rhs Right-hand region view state.
    \return True when both region view states store equal values.
    */
    friend bool operator==(const ToneRegionViewState& lhs, const ToneRegionViewState& rhs) =
        default;
};

/*! \brief View-facing state for the tone track row below the backing waveform. */
struct ToneTrackViewState
{
    /*! \brief Tone regions in ascending start order. */
    std::vector<ToneRegionViewState> regions;

    /*!
    \brief Compares two tone-track view states by their stored values.
    \param lhs Left-hand tone-track view state.
    \param rhs Right-hand tone-track view state.
    \return True when both tone-track view states store equal regions.
    */
    friend bool operator==(const ToneTrackViewState& lhs, const ToneTrackViewState& rhs) = default;
};

} // namespace rock_hero::editor::core
