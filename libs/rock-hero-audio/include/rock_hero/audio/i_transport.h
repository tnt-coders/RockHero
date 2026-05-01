/*!
\file i_transport.h
\brief Tracktion-free transport control port.
*/

#pragma once

#include <rock_hero/audio/transport_state.h>
#include <rock_hero/core/timeline.h>

namespace rock_hero::audio
{

/*!
\brief Project-owned transport control boundary.

The current contract is message-thread-only. Callers must invoke control methods, state(),
position(), addListener(), and removeListener() from the message thread. Listener callbacks are
also delivered on the message thread.

state() and position() are live reads. Listener callbacks are reserved for coarse TransportState
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

    /*! \brief Stops playback and resets the current transport position. */
    virtual void stop() = 0;

    /*!
    \brief Moves the transport to a new timeline position.
    \param position The target playback position.
    */
    virtual void seek(core::TimePosition position) = 0;

    /*!
    \brief Reads the current coarse transport state.

    This method is message-thread-only and is not thread-safe.

    \return Current transport state.
    */
    [[nodiscard]] virtual TransportState state() const noexcept = 0;

    /*!
    \brief Reads the current transport position for render-cadence cursor drawing.

    Like state(), this method is a live message-thread-only read and is not thread-safe. Listener
    callbacks report only TransportState changes and do not carry position. UI code that needs
    smooth cursor motion should call position() at its own render cadence.

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
