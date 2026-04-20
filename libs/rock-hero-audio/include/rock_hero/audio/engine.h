/*!
\file engine.h
\brief Isolation layer between Tracktion Engine and the rest of the application.
*/

#pragma once

#include <cstddef>
#include <filesystem>
#include <functional>
#include <memory>

namespace juce
{
class Component;
} // namespace juce

namespace rock_hero::audio
{

class Thumbnail;

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
    \brief RAII handle representing an active subscription to an Engine event.

    Returned by Engine::subscribeOnPlayingStateChanged(). The handle unsubscribes the
    callback when it is destroyed.

    Neither copyable nor movable by design: for its entire lifetime, a Subscription
    represents a live registration on an Engine. The location where it is constructed is
    the location where the subscription lives. Factory return uses C++17 guaranteed copy
    elision, so a Subscription can still be returned by value and stored as a member.

    The handle must not outlive the Engine that produced it. Keep it as a member of the
    consumer so that consumer destruction cancels the subscription before the Engine is
    torn down.
    */
    class Subscription
    {
    public:
        /*! \brief Unsubscribes the callback. */
        ~Subscription();

        Subscription(const Subscription&) = delete;
        Subscription& operator=(const Subscription&) = delete;
        Subscription(Subscription&&) = delete;
        Subscription& operator=(Subscription&&) = delete;

    private:
        friend class Engine;

        enum class Kind
        {
            playing_state,
            transport_position
        };

        class Key
        {
        private:
            Key() = default;
            friend class Engine;
        };

        Subscription(Key key, Engine* engine, std::size_t id, Kind kind) noexcept;

        Engine* m_engine{nullptr};
        std::size_t m_id{0};
        Kind m_kind{Kind::playing_state};
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

    /*!
    \brief Registers a callback fired on transitions between playing and not-playing.

    The callback runs on the message thread. It fires only on actual transitions; seeks,
    loads, and other transport events that leave the playing flag unchanged do not invoke
    it. Multiple independent subscribers are supported; each gets its own handle.

    \param callback Invoked with the new playing state on each transition.
    \return A non-copyable, non-movable RAII handle; destroying it unsubscribes the callback.
    The caller owns the handle and must not let it outlive this Engine.
    */
    [[nodiscard]] Subscription subscribeOnPlayingStateChanged(
        std::function<void(bool playing)> callback);

    /*!
    \brief Registers a callback fired when the transport position changes.

    The callback runs on the message thread after Tracktion publishes a new transport position.
    Subscribers receive seconds clamped to the currently loaded audio duration.

    \param callback Invoked with the new transport position in seconds.
    \return A non-copyable, non-movable RAII handle; destroying it unsubscribes the callback.
    The caller owns the handle and must not let it outlive this Engine.
    */
    [[nodiscard]] Subscription subscribeOnTransportPositionChanged(
        std::function<void(double seconds)> callback);

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace rock_hero::audio
