/*!
\file scoped_listener.h
\brief Reference-based RAII listener registration helper.
*/

#pragma once

namespace rock_hero::audio
{

/*!
\brief Attaches a listener to a broadcaster for this object's lifetime.

Constructs with (broadcaster, listener) and calls broadcaster.addListener(listener). The
destructor calls broadcaster.removeListener(listener). Non-copyable and non-movable so that each
instance represents exactly one live registration.

Declare a ScopedListener member after the listener's dependent state so that its destructor runs
before that state is destroyed.

\tparam Broadcaster Any type exposing addListener / removeListener taking Listener by reference.
\tparam Listener The listener interface type attached to the broadcaster.
*/
template <class Broadcaster, class Listener> class ScopedListener
{
public:
    /*!
    \brief Attaches the listener to the broadcaster.
    \param broadcaster The broadcaster to register with; must outlive this ScopedListener.
    \param listener The listener to attach; must outlive this ScopedListener.
    */
    ScopedListener(Broadcaster& broadcaster, Listener& listener) noexcept
        : m_broadcaster(broadcaster)
        , m_listener(listener)
    {
        m_broadcaster.addListener(m_listener);
    }

    /*! \brief Detaches the listener from the broadcaster. */
    ~ScopedListener()
    {
        m_broadcaster.removeListener(m_listener);
    }

    /*! \brief Copying is disabled so one object always represents one registration. */
    ScopedListener(const ScopedListener&) = delete;

    /*! \brief Copy assignment is disabled to prevent duplicate listener registrations. */
    ScopedListener& operator=(const ScopedListener&) = delete;

    /*! \brief Moving is disabled because it would change which object owns deregistration. */
    ScopedListener(ScopedListener&&) = delete;

    /*! \brief Move assignment is disabled because registration ownership is fixed. */
    ScopedListener& operator=(ScopedListener&&) = delete;

private:
    // Broadcaster that owns the listener list this registration is attached to.
    Broadcaster& m_broadcaster;

    // Listener detached from m_broadcaster during destruction.
    Listener& m_listener;
};

} // namespace rock_hero::audio
