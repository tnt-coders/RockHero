/*!
\file audio_meter_snapshot.h
\brief Project-owned peak meter values exposed by the audio backend.
*/

#pragma once

#include <algorithm>
#include <type_traits>

namespace rock_hero::common::audio
{

/*! \brief Lowest decibel value shown by Rock Hero peak meters. */
[[nodiscard]] constexpr double minimumAudioMeterDb() noexcept
{
    return -60.0;
}

/*! \brief Decibel value treated as clipping for Rock Hero peak meters. */
[[nodiscard]] constexpr double clippingAudioMeterDb() noexcept
{
    return 0.0;
}

/*! \brief Peak level and clipping state for a single meter. */
struct AudioMeterLevel
{
    /*! \brief Peak level in decibels full scale. */
    double peak_db{minimumAudioMeterDb()};

    /*! \brief True when the most recent meter window reached or exceeded clipping. */
    bool clipping{false};

    /*!
    \brief Compares two audio meter levels by their stored values.
    \param lhs Left-hand audio meter level.
    \param rhs Right-hand audio meter level.
    \return True when both levels store equal peak and clipping values.
    */
    friend constexpr bool operator==(AudioMeterLevel lhs, AudioMeterLevel rhs) noexcept = default;
};

static_assert(sizeof(AudioMeterLevel) <= 16);
static_assert(std::is_trivially_copyable_v<AudioMeterLevel>);

/*! \brief Current display meter values read from the audio backend. */
struct AudioMeterSnapshot
{
    /*! \brief Live rig level after the input gain fader and before user plugins. */
    AudioMeterLevel live_rig_input;

    /*! \brief Live rig level after user plugins and the output gain fader. */
    AudioMeterLevel live_rig_output;

    /*! \brief Final mixed output level after backing track and live rig are summed. */
    AudioMeterLevel master_output;

    /*!
    \brief Compares two audio meter snapshots by their stored values.
    \param lhs Left-hand audio meter snapshot.
    \param rhs Right-hand audio meter snapshot.
    \return True when all meter levels match.
    */
    friend constexpr bool operator==(AudioMeterSnapshot lhs, AudioMeterSnapshot rhs) noexcept =
        default;
};

static_assert(sizeof(AudioMeterSnapshot) <= 48);
static_assert(std::is_trivially_copyable_v<AudioMeterSnapshot>);

/*!
\brief Maps a meter level to a display fraction.
\param peak_db Peak decibel value to map.
\return Fraction in [0, 1], where 1.0 is clipping.
*/
[[nodiscard]] constexpr double audioMeterFraction(double peak_db) noexcept
{
    return std::clamp(
        (peak_db - minimumAudioMeterDb()) / (clippingAudioMeterDb() - minimumAudioMeterDb()),
        0.0,
        1.0);
}

} // namespace rock_hero::common::audio
