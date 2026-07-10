/*!
\file audio_normalization.h
\brief Framework-free normalization metadata persisted on backing audio assets.
*/

#pragma once

#include <compare>
#include <string>

namespace rock_hero::common::core
{

/*!
\brief Target loudness used when computing backing audio normalization gain.

Runtime-only parameter for the analysis function; not persisted in project packages. The peak
clamp is implicit: applied gain is clamped so the loudest sample does not exceed 0 dBFS.
*/
struct AudioNormalizationTarget
{
    /*! \brief Requested integrated loudness in LUFS. */
    double integrated_loudness_lufs{-16.0};

    /*!
    \brief Compares two targets by their stored fields.
    \param lhs Left-hand target.
    \param rhs Right-hand target.
    \return True when both targets store equal fields.
    */
    friend bool operator==(const AudioNormalizationTarget& lhs, const AudioNormalizationTarget& rhs)
    {
        // Hand-written, not defaulted: a defaulted comparison trips clang's -Wfloat-equal on the
        // floating member. Exact equality is intended; the ordering query expresses it warning-
        // free with identical semantics (NaN compares unequal either way).
        return std::is_eq(lhs.integrated_loudness_lufs <=> rhs.integrated_loudness_lufs);
    }
};

/*!
\brief Minimal normalization record persisted on an AudioAsset.

Stores the playback gain and a validation hash that proves the gain still belongs to the current
audio file. The hash covers both the audio file bytes and the gain value (formatted at one decimal
places), so changing either invalidates the record and triggers re-analysis on next project open
or import.
*/
struct AudioNormalization
{
    /*! \brief Gain in decibels to apply during playback and waveform drawing. */
    double gain_db{0.0};

    /*!
    \brief SHA-256 hash validating the pair of gain_db and the current audio file bytes.

    Computed from a stable byte sequence that includes a version prefix, the gain formatted at
    one decimal place, and the raw audio file content. If the audio file is modified or gain_db
    is changed without recomputing the hash, validation fails and LUFS-I analysis runs before
    the project opens.
    */
    std::string validation_sha256;

    /*!
    \brief Compares two normalization records by their stored fields.
    \param lhs Left-hand normalization record.
    \param rhs Right-hand normalization record.
    \return True when both normalization records store equal fields.
    */
    friend bool operator==(const AudioNormalization& lhs, const AudioNormalization& rhs)
    {
        // Hand-written, not defaulted: a defaulted comparison trips clang's -Wfloat-equal on the
        // floating member. Exact equality is intended; the ordering query expresses it warning-
        // free with identical semantics (NaN compares unequal either way).
        return std::is_eq(lhs.gain_db <=> rhs.gain_db) &&
               lhs.validation_sha256 == rhs.validation_sha256;
    }
};

} // namespace rock_hero::common::core
