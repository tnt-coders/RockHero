/*!
\file audio_asset.h
\brief Framework-free reference to an audio asset used by sessions and adapters.
*/

#pragma once

#include <filesystem>

namespace rock_hero::core
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
    \brief Compares two asset references by their stored path value.
    \param lhs Left-hand asset reference.
    \param rhs Right-hand asset reference.
    \return True when both assets store equal paths.
    */
    friend bool operator==(const AudioAsset& lhs, const AudioAsset& rhs) = default;
};

} // namespace rock_hero::core
