/*!
\file i_audio_meter_source.h
\brief Read-only peak meter source for continuous audio display.
*/

#pragma once

#include <rock_hero/common/audio/audio_meter_snapshot.h>

namespace rock_hero::common::audio
{

/*!
\brief Provides current audio meter levels without exposing Tracktion types.

Meter snapshots are display data sampled by the UI at paint or vblank cadence. They are not
project state and should not be persisted.
*/
class IAudioMeterSource
{
public:
    /*! \brief Destroys the audio meter source interface. */
    virtual ~IAudioMeterSource() = default;

    /*!
    \brief Reads the latest meter values.
    \return Current peak levels, or silent values when no backend meter is active.
    */
    [[nodiscard]] virtual AudioMeterSnapshot audioMeterSnapshot() const = 0;

protected:
    /*! \brief Creates the audio meter source interface. */
    IAudioMeterSource() = default;

    /*! \brief Copies the audio meter source interface. */
    IAudioMeterSource(const IAudioMeterSource&) = default;

    /*! \brief Moves the audio meter source interface. */
    IAudioMeterSource(IAudioMeterSource&&) = default;

    /*!
    \brief Assigns the audio meter source interface from another interface.
    \return Reference to this audio meter source interface.
    */
    IAudioMeterSource& operator=(const IAudioMeterSource&) = default;

    /*!
    \brief Move-assigns the audio meter source interface from another interface.
    \return Reference to this audio meter source interface.
    */
    IAudioMeterSource& operator=(IAudioMeterSource&&) = default;
};

} // namespace rock_hero::common::audio
