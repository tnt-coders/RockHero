/*!
\file audio_asset.h
\brief Framework-free reference to an audio asset used by sessions and adapters.
*/

#pragma once

#include <filesystem>
#include <optional>
#include <rock_hero/common/core/audio_loudness_metadata.h>

namespace rock_hero::common::core
{

/*!
\brief Identifies an audio file or file-like asset without depending on JUCE.

The value is intentionally lightweight. Adapters translate the path into framework-specific
file objects at the boundary where audio loading or waveform generation happens.
*/
struct AudioAsset
{
    /*! \brief Filesystem path for the asset selected by the user or loaded from a song. */
    std::filesystem::path path;

    /*!
    \brief Loudness record describing the asset when last normalized or analyzed.

    Present after the editor has normalized this asset against a known target and persisted the
    resulting analysis. Absent for assets loaded from older project packages that pre-date
    loudness metadata; open-time policy schedules background analysis in that case.
    */
    std::optional<AudioLoudnessMetadata> loudness_metadata;

    /*!
    \brief Compares two asset references by their stored fields.
    \param lhs Left-hand asset reference.
    \param rhs Right-hand asset reference.
    \return True when both assets store equal fields.
    */
    friend bool operator==(const AudioAsset& lhs, const AudioAsset& rhs) = default;
};

} // namespace rock_hero::common::core
