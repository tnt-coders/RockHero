/*!
\file engine.h
\brief Isolation layer between Tracktion Engine and the rest of the application.
*/

#pragma once

#include <filesystem>
#include <memory>

namespace juce
{
class Component;
} // namespace juce

namespace rock_hero::audio
{

class Thumbnail;

/*!
\brief Generic RAII helper that attaches a listener to a broadcaster for its lifetime.

Constructs with (broadcaster, listener) and calls broadcaster.addListener(&listener). The
destructor calls broadcaster.removeListener(&listener). Non-copyable and non-movable so that
for its entire lifetime it represents exactly one live registration.

Declare a ScopedListener member last in the owning class so that its destructor runs first
during teardown, ensuring the listener is detached before other members (including those
consulted by in-flight callbacks) are destroyed.

\tparam Broadcaster Any type exposing addListener / removeListener taking a pointer-to-Listener.
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
        : m_broadcaster(broadcaster), m_listener(listener)
    {
        m_broadcaster.addListener(&m_listener);
    }

    /*! \brief Detaches the listener from the broadcaster. */
    ~ScopedListener()
    {
        m_broadcaster.removeListener(&m_listener);
    }

    ScopedListener(const ScopedListener&) = delete;
    ScopedListener& operator=(const ScopedListener&) = delete;
    ScopedListener(ScopedListener&&) = delete;
    ScopedListener& operator=(ScopedListener&&) = delete;

private:
    Broadcaster& m_broadcaster;
    Listener& m_listener;
};

/*!
\brief Isolation layer between Tracktion Engine and the rest of the application.

All other code depends on this interface rather than on Tracktion directly. This boundary
enables a fallback-to-raw-JUCE strategy: only rock-hero-audio implementation files
include Tracktion headers.

Owns the tracktion::Engine and the single tracktion::Edit used for playback.
All public methods except getTransportPosition() must be called on the message thread.

\see rock_hero::MainWindow
\see rock_hero::ui::WaveformDisplay
*/
class Engine
{
public:
    /*!
    \brief Listener interface for engine transport events.

    Callbacks fire on the message thread. Default implementations are empty so consumers
    override only the events they care about. Use audio::ScopedListener to attach and
    detach instances safely; declare the ScopedListener member last in the owning class.

    Events are pre-filtered: enginePlayingStateChanged() fires only on genuine transitions,
    not on every underlying transport change.

    \see ScopedListener
    \see Engine::addListener
    */
    class Listener
    {
    public:
        virtual ~Listener() = default;

        /*!
        \brief Fires on transitions between playing and not-playing.

        Does not fire for seeks, loads, or other transport events that leave the playing
        flag unchanged.

        \param playing True when the transport is now playing.
        */
        virtual void enginePlayingStateChanged(bool playing)
        {
            static_cast<void>(playing);
        }

        /*!
        \brief Fires when the transport position changes.

        The value is clamped to the currently loaded audio duration.

        \param seconds New transport position in seconds.
        */
        virtual void engineTransportPositionChanged(double seconds)
        {
            static_cast<void>(seconds);
        }

    protected:
        /*! \brief Constructs a listener base. */
        Listener() = default;

        /*! \brief Copies the listener base subobject for derived listener types. */
        Listener(const Listener&) = default;

        /*! \brief Moves the listener base subobject for derived listener types. */
        Listener(Listener&&) = default;

        /*!
        \brief Copies the listener base subobject for derived listener types.
        \return This listener base.
        */
        Listener& operator=(const Listener&) = default;

        /*!
        \brief Moves the listener base subobject for derived listener types.
        \return This listener base.
        */
        Listener& operator=(Listener&&) = default;
    };

    /*!
    \brief Creates the Tracktion Engine instance and a single-track Edit for playback.

    Initialises the device manager with stereo output only.
    */
    Engine();

    /*! \brief Stops transport and tears down Tracktion objects in dependency order. */
    ~Engine();

    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;
    Engine(Engine&&) = delete;
    Engine& operator=(Engine&&) = delete;

    /*!
    \brief Registers a listener for engine transport events.

    The listener must remain alive until removeListener() is called or the Engine is
    destroyed. Prefer ScopedListener<Engine, Engine::Listener> for automatic lifetime
    management.

    \param listener Non-null pointer; must outlive the registration.
    */
    void addListener(Listener* listener);

    /*!
    \brief Unregisters a previously added listener.
    \param listener The same pointer previously passed to addListener().
    */
    void removeListener(Listener* listener);

    /*!
    \brief Loads an audio file onto track 0, replacing any existing clip.

    Stops playback before mutating clips to avoid mid-stream graph rebuilds. Must be called on
    the message thread.

    \param file The audio file to load.
    \return True if the clip was inserted successfully.
    */
    bool loadFile(const std::filesystem::path& file);

    /*! \brief Starts transport playback. */
    void play();

    /*! \brief Stops transport playback and resets the position to the beginning. */
    void stop();

    /*!
    \brief Pauses transport playback, preserving the current position.

    Use this when the user wants to resume from the same point. Contrast with stop(), which
    resets the position to the beginning.
    */
    void pause();

    /*!
    \brief Seeks the transport to the given position in seconds.

    Safe to call while playing (playback continues from the new position) or while stopped/paused
    (only the cursor moves). Clamps to [0, loaded audio length].

    \param seconds Target position in seconds.
    */
    void seek(double seconds);

    /*!
    \brief Reports whether the transport is currently playing.
    \return True when the transport is currently playing.
    */
    [[nodiscard]] bool isPlaying() const;

    /*!
    \brief Returns the current transport position in seconds.

    Lock-free; safe to call from any thread. The value is backed by a std::atomic and currently
    written from Tracktion transport position change events on the message thread. It will be
    moved to an audio-thread callback once ASIO input is wired.

    \return The cached transport position in seconds.
    */
    [[nodiscard]] double getTransportPosition() const noexcept;

    /*!
    \brief Creates a Thumbnail bound to this engine.

    Factory method that passes the internal Tracktion Engine to the thumbnail without exposing
    it through the public API.

    \param owner The component that should be repainted when the proxy finishes generating.
    \return A new Thumbnail instance.
    */
    [[nodiscard]] std::unique_ptr<Thumbnail> createThumbnail(juce::Component& owner);

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace rock_hero::audio
