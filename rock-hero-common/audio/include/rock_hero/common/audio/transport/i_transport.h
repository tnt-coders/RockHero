/*!
\file i_transport.h
\brief Tracktion-free transport control port.
*/

#pragma once

#include <expected>
#include <optional>
#include <rock_hero/common/audio/transport/transport_error.h>
#include <rock_hero/common/audio/transport/transport_state.h>
#include <rock_hero/common/core/timeline/timeline.h>

namespace rock_hero::common::audio
{

/*!
\brief Shortest loop region the transport contract accepts, in seconds.

Enforced by every implementation before any backend call, so backend-specific minimum-length
quirks are never reachable and loop rejection always surfaces as the same typed error. Shared
publicly so fakes and tests enforce the identical contract.
*/
inline constexpr common::core::TimeDuration g_minimum_loop_region_duration{0.1};

/*!
\brief Project-owned transport control boundary.

The current contract is message-thread-only. Callers must invoke control methods, state(),
position(), addListener(), and removeListener() from the message thread. Listener callbacks are
also delivered on the message thread.

state() and position() are current reads. Listener callbacks are reserved for coarse TransportState
changes such as play/pause/stop; they do not carry position and are not emitted for every playhead
movement. UI code that needs smooth cursor motion should call position() at its own render cadence.
*/
class ITransport
{
public:
    /*! \brief Receives coarse transition-shaped transport state snapshots. */
    class Listener
    {
    public:
        /*! \brief Destroys the listener interface. */
        virtual ~Listener() = default;

        /*!
        \brief Handles a changed transport state snapshot.
        \param state Current message-thread state after a coarse transport transition.
        */
        virtual void onTransportStateChanged(TransportState state) = 0;

    protected:
        /*! \brief Creates the listener interface. */
        Listener() = default;

        /*! \brief Copies the listener interface. */
        Listener(const Listener&) = default;

        /*! \brief Moves the listener interface. */
        Listener(Listener&&) = default;

        /*!
        \brief Assigns the listener interface from another listener.
        \return Reference to this listener interface.
        */
        Listener& operator=(const Listener&) = default;

        /*!
        \brief Move-assigns the listener interface from another listener.
        \return Reference to this listener interface.
        */
        Listener& operator=(Listener&&) = default;
    };

    /*! \brief Destroys the transport interface. */
    virtual ~ITransport() = default;

    /*! \brief Starts playback from the current transport position. */
    virtual void play() = 0;

    /*! \brief Pauses playback without resetting the current transport position. */
    virtual void pause() = 0;

    /*! \brief Stops playback and resets the current transport position to a clean start. */
    virtual void stop() = 0;

    /*!
    \brief Moves the transport to a new timeline position.
    \param position The target playback position.
    */
    virtual void seek(common::core::TimePosition position) = 0;

    /*!
    \brief Requests a playback speed factor for the backing content.

    The live instrument path is never speed-affected — speed applies to backing playback only.
    Current implementations accept exactly 1.0; any other factor returns
    TransportErrorCode::SpeedNotSupported and leaves playback unchanged. Practice-speed support
    (docs/roadmap/28-practice-mode.md) widens the accepted range behind this same signature.

    \param factor Requested playback speed multiplier, where 1.0 is normal speed.
    \return Nothing on success, or a typed transport error when the factor is unsupported.
    */
    [[nodiscard]] virtual std::expected<void, TransportError> setPlaybackSpeed(double factor) = 0;

    /*!
    \brief Reads the playback speed factor currently applied to backing playback.

    Message-thread-only like the rest of the port.

    \return Current playback speed multiplier; 1.0 until practice-speed support lands.
    */
    [[nodiscard]] virtual double playbackSpeed() const noexcept = 0;

    /*!
    \brief Engages loop playback over a region of the edit timeline.

    Endpoints are normalized (a reversed range is swapped, never rejected) before the minimum
    length check. Regions shorter than g_minimum_loop_region_duration return
    TransportErrorCode::LoopRegionTooShort and leave any previously engaged loop untouched. Loop
    wrap behaves like a seek: automation and parameter streams resync automatically after the
    jump. Loading a different arrangement clears the engaged loop; callers that want the loop to
    survive a load must re-apply it afterwards.

    \param region Loop region in edit-timeline seconds; endpoints may arrive in either order.
    \return Nothing on success, or a typed transport error when the region is too short.
    */
    [[nodiscard]] virtual std::expected<void, TransportError> setLoopRegion(
        common::core::TimeRange region) = 0;

    /*!
    \brief Disengages loop playback, leaving position and play state untouched.

    Clearing when no loop is engaged is a no-op.
    */
    virtual void clearLoopRegion() = 0;

    /*!
    \brief Reads the currently engaged loop region.

    Message-thread-only like the rest of the port.

    \return Normalized engaged loop region, or std::nullopt when looping is disengaged.
    */
    [[nodiscard]] virtual std::optional<common::core::TimeRange> loopRegion() const noexcept = 0;

    /*!
    \brief Reads the current coarse transport state.

    This method is message-thread-only and is not thread-safe.

    \return Current transport state.
    */
    [[nodiscard]] virtual TransportState state() const noexcept = 0;

    /*!
    \brief Reads the current transport position for render-cadence cursor drawing.

    Like state(), this method is a current message-thread-only read and is not thread-safe. Listener
    callbacks report only TransportState changes and do not carry position. UI code that needs
    smooth cursor motion should call position() at its own render cadence.

    \return Current transport position.
    */
    [[nodiscard]] virtual common::core::TimePosition position() const noexcept = 0;

    /*!
    \brief Registers a non-owning transport listener.
    \param listener The listener to notify until it is removed.
    */
    virtual void addListener(Listener& listener) = 0;

    /*!
    \brief Removes a previously registered transport listener.
    \param listener The same listener object previously registered with addListener().
    */
    virtual void removeListener(Listener& listener) = 0;

protected:
    /*! \brief Creates the transport interface. */
    ITransport() = default;

    /*! \brief Copies the transport interface. */
    ITransport(const ITransport&) = default;

    /*! \brief Moves the transport interface. */
    ITransport(ITransport&&) = default;

    /*!
    \brief Assigns the transport interface from another transport interface.
    \return Reference to this transport interface.
    */
    ITransport& operator=(const ITransport&) = default;

    /*!
    \brief Move-assigns the transport interface from another transport interface.
    \return Reference to this transport interface.
    */
    ITransport& operator=(ITransport&&) = default;
};

} // namespace rock_hero::common::audio
