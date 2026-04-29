/*!
\file i_transport.h
\brief Tracktion-free transport control port.
*/

#pragma once

#include <rock_hero/audio/transport_status.h>
#include <rock_hero/core/timeline.h>

namespace rock_hero::audio
{

/*!
\brief Project-owned transport control boundary.

The current contract is owned by the message thread. Callers must invoke control methods,
status(), addListener(), and removeListener() from the message thread. Listener callbacks are also
delivered on the message thread.

Continuous transport position is available through position(). Listener callbacks are reserved for
coarse transition-shaped changes such as play/pause/stop or duration changes, so UI code that
needs smooth cursor motion should call position() at its own render cadence rather than waiting for
listener delivery.
*/
class ITransport
{
public:
    /*! \brief Receives coarse transition-shaped transport status snapshots. */
    class Listener
    {
    public:
        /*! \brief Destroys the listener interface. */
        virtual ~Listener() = default;

        /*!
        \brief Handles a changed transport status snapshot.

        This callback is intended for transition-shaped updates rather than for every position tick.
        Callers that need smooth playback cursor motion should call position() at their own
        render cadence instead.

        \param status Current message-thread status after a coarse transport transition.
        */
        virtual void onTransportStatusChanged(const TransportStatus& status) = 0;

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

    /*! \brief Stops playback and resets the current transport position. */
    virtual void stop() = 0;

    /*!
    \brief Moves the transport to a new timeline position.
    \param position The target playback position.
    */
    virtual void seek(core::TimePosition position) = 0;

    /*!
    \brief Returns the current coarse transport status snapshot.
    \return The current message-thread transport status snapshot.
    */
    [[nodiscard]] virtual TransportStatus status() const = 0;

    /*!
    \brief Reads the current transport position for render-cadence cursor drawing.

    Unlike status(), this method is a live position read rather than a listener-published snapshot.
    UI code that needs smooth cursor motion should call this at its own render cadence and use
    EditorViewState or equivalent discrete state for duration and visible range mapping.

    \return Current transport position.
    */
    [[nodiscard]] virtual core::TimePosition position() const noexcept = 0;

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

} // namespace rock_hero::audio
