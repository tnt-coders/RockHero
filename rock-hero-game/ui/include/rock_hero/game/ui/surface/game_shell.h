/*!
\file game_shell.h
\brief Composition point owning the game window, render device, and main frame loop.
*/

#pragma once

#include <cstdint>
#include <optional>

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
    \brief Runs the game surface until quit: window + JUCE runtime + render device + frame loop.

    Must be called on the process main thread — the first JUCE initialization inside binds the
    calling thread as the JUCE message thread for the rest of the process lifetime.

    \param options Run options; see \ref GameShellOptions.
    \return Process exit code: zero on a clean quit, nonzero when startup fails.
    */
    [[nodiscard]] int run(const GameShellOptions& options);
};

} // namespace rock_hero::game::ui
