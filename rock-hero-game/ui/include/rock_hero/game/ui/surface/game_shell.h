/*!
\file game_shell.h
\brief Composition point owning the game window, render device, and main frame loop.
*/

#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>

namespace rock_hero::game::core
{
class GameplaySession;
} // namespace rock_hero::game::core

namespace rock_hero::game::ui
{

/*! \brief Options controlling a \ref GameShell run. */
struct GameShellOptions
{
    /*!
    \brief When set, the loop exits cleanly after this many frames.

    Diagnostic hook for smoke runs and automated verification; normal runs leave it empty and exit
    through the window's quit request.
    */
    std::optional<std::uint64_t> frame_limit;

    /*!
    \brief When set, loads this .rock package at startup and scrolls its first charted
    arrangement on the highway against a development clock.

    Development hook (plan 25 Phase 3): song selection belongs to plan 26's library, and the real
    playback clock arrives with plan 21's engine. Empty means no chart is loaded and the highway
    renders an empty board.
    */
    std::optional<std::filesystem::path> dev_package;

    /*!
    \brief True to mirror the highway for left-handed display.

    Development flag until plan 26/27 surface the user-facing setting; the mirror itself is pure
    math inside the shared highway projection and camera.
    */
    bool lefty{false};

    /*!
    \brief True activates the dev-diagnostics layer (plan 20 Phase 4, 20-Q5: A).

    Compiled into every build; this runtime flag turns on the diagnostics overlay and its key
    toggles, chart hot-reload, section seeking, and the bgfx debug checks. Players never pass it.
    */
    bool dev_mode{false};

    /*!
    \brief Non-owning gameplay session composed by the app (plan 21 Phase 6).

    Composition lives in `app/` per the decided GameShell watch item: main.cpp constructs the
    audio engine and the session and injects them here; the shell only drives them from its loop
    and input wiring. Null runs the shell without audio (the pre-engine dev fixture behavior).
    The caller owns lifetime: the session must outlive run() and be closed by the caller after
    run() returns.
    */
    core::GameplaySession* gameplay_session{nullptr};

    /*!
    \brief Session-private scratch directory for the extracted package, composed by the app.

    Passed through to GameplaySessionRequest::workspace_directory when the shell starts the
    session over dev_package; unused when gameplay_session is null.
    */
    std::filesystem::path session_workspace_directory;
};

/*!
\brief Owns the game process surface: the SDL window, the bgfx device, and the frame loop.

Implements loop model L2 from the G20-RENDER gate (§ Gate record in
docs/roadmap/20-game-architecture-and-render-stack.md): SDL owns the frame loop on the calling
thread, which JUCE binds as its
message thread, and every frame polls SDL events, drains JUCE's pending message queue with a
bounded dispatch loop, and submits the bgfx frame. Later phases grow this type into the full game
composition (resources, playback clock, diagnostics), so it is a class rather than a free function
even while its only state is the loop.
*/
class GameShell
{
public:
    /*!
    \brief Runs the game surface until quit: window + render device + frame loop.

    Must be called on the process main thread, and the caller must already own the JUCE runtime
    (a juce::ScopedJuceInitialiser_GUI in main.cpp, constructed before the audio engine) — its
    first initialization binds the calling thread as the JUCE message thread for the process
    lifetime, and the loop's per-frame drain services everything JUCE from then on.

    \param options Run options; see \ref GameShellOptions.
    \return Process exit code: zero on a clean quit, nonzero when startup fails.
    */
    [[nodiscard]] int run(const GameShellOptions& options);
};

} // namespace rock_hero::game::ui
