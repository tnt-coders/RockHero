/*!
\file i_mix_controls.h
\brief Port for the product-facing mix gains: edit-wide master and backing-track volume.
*/

#pragma once

#include <expected>
#include <rock_hero/common/audio/mix/mix_controls_error.h>
#include <rock_hero/common/audio/shared/gain.h>

namespace rock_hero::common::audio
{

/*!
\brief Project-owned boundary for the coarse product mix: master and backing volumes.

Exactly three user-facing volumes exist and each has one owner: the edit-wide master gain and
the backing-track gain live here; the player-monitor gain is ILiveRig::outputGain /
setOutputGain and is deliberately NOT duplicated on this port. The backing gain is a
track-level stage that composes with (never overwrites) the per-clip normalization gain the
song loader applies. All methods are message-thread operations, like the rest of the engine
ports; changes are parameter moves only and never rebuild the playback graph.
*/
class IMixControls
{
public:
    /*! \brief Destroys the mix controls interface. */
    virtual ~IMixControls() = default;

    /*!
    \brief Sets the edit-wide master gain, scaling everything audible.
    \param gain Desired master gain; clamped to the accepted range.
    \return Empty success, or a typed failure when the master stage is unavailable.
    */
    [[nodiscard]] virtual std::expected<void, MixControlsError> setMasterGain(Gain gain) = 0;

    /*!
    \brief Reads the edit-wide master gain.
    \return Current master gain.
    */
    [[nodiscard]] virtual Gain masterGain() const = 0;

    /*!
    \brief Sets the backing-track gain; composes with the clip normalization gain.
    \param gain Desired backing gain; clamped to the accepted range.
    \return Empty success, or a typed failure when the backing track is unavailable.
    */
    [[nodiscard]] virtual std::expected<void, MixControlsError> setBackingGain(Gain gain) = 0;

    /*!
    \brief Reads the backing-track gain.
    \return Current backing gain.
    */
    [[nodiscard]] virtual Gain backingGain() const = 0;

protected:
    /*! \brief Creates the mix controls interface. */
    IMixControls() = default;

    /*! \brief Copies the mix controls interface. */
    IMixControls(const IMixControls&) = default;

    /*! \brief Moves the mix controls interface. */
    IMixControls(IMixControls&&) = default;

    /*!
    \brief Assigns the mix controls interface from another instance.
    \return Reference to this mix controls interface.
    */
    IMixControls& operator=(const IMixControls&) = default;

    /*!
    \brief Move-assigns the mix controls interface from another instance.
    \return Reference to this mix controls interface.
    */
    IMixControls& operator=(IMixControls&&) = default;
};

} // namespace rock_hero::common::audio
