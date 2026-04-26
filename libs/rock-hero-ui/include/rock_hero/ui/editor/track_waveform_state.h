/*!
\file track_waveform_state.h
\brief Framework-free waveform-row state used by editor view contracts.
*/

#pragma once

#include <optional>
#include <rock_hero/core/track.h>
#include <string>

namespace rock_hero::ui
{

/*!
\brief View-facing state for one waveform row in the editor.

This state stays focused on the information the current editor UI can actually render. It does not
speculate about mute, gain, selection, or per-row transport state.
*/
struct TrackWaveformState
{
    /*! \brief Stable id of the session track represented by this waveform row. */
    core::TrackId track_id;

    /*! \brief User-visible label shown for the row. */
    std::string display_name;

    /*! \brief Optional audio asset currently assigned to the row. */
    std::optional<core::AudioAsset> audio_asset;

    /*!
    \brief Compares two waveform-row states by their stored values.
    \param lhs Left-hand waveform-row state.
    \param rhs Right-hand waveform-row state.
    \return True when both waveform-row states store equal values.
    */
    friend bool operator==(const TrackWaveformState& lhs, const TrackWaveformState& rhs) = default;
};

} // namespace rock_hero::ui
