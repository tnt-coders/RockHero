/*!
\file audio_asset.h
\brief Framework-free reference to an audio asset used by sessions and adapters.
*/

#pragma once

#include <filesystem>
#include <optional>
#include <rock_hero/common/core/song/audio_normalization.h>

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
    \brief Normalization record describing the playback gain and its validation hash.

    Present after the editor has analyzed this asset against a loudness target and persisted the
    resulting gain. Absent for assets loaded from packages that pre-date normalization metadata
    or from formats that do not carry it; the open/import flow analyzes those before the project
    becomes usable.
    */
    std::optional<AudioNormalization> normalization;

    /*!
    \brief Compares two asset references by their stored fields.
    \param lhs Left-hand asset reference.
    \param rhs Right-hand asset reference.
    \return True when both assets store equal fields.
    */
    friend bool operator==(const AudioAsset& lhs, const AudioAsset& rhs) = default;
};

} // namespace rock_hero::common::core
