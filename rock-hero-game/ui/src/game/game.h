/*!
\file game.h
\brief Game content object: highway scene, menu, diagnostics, and per-frame update/render.
*/

#pragma once

#include "dev/dev_session.h"
#include "overlay/diagnostics_overlay.h"
#include "surface/game_window.h"

#include <chrono>
#include <filesystem>
#include <optional>
#include <rock_hero/common/audio/clock/playback_clock_snapshot.h>
#include <rock_hero/common/ui/highway/highway_renderer.h>
#include <rock_hero/game/core/diagnostics/diagnostics.h>
#include <rock_hero/game/core/frame_clock/frame_clock.h>
#include <rock_hero/game/core/input/menu_bindings.h>
#include <rock_hero/game/core/library/library_index.h>
#include <rock_hero/game/core/menu/song_select_menu.h>

namespace rock_hero::common::ui
{
class RenderDevice;
} // namespace rock_hero::common::ui

namespace rock_hero::game::core
{
class GameplaySession;
} // namespace rock_hero::game::core

namespace rock_hero::game::ui
{

/*!
\brief Owns the game's on-screen content and drives it one frame at a time.

Game is the content object in the game/ui composition, the counterpart of the editor's
`editor::ui::Editor`: it owns the highway renderer, the song-selection menu, the dev-diagnostics
layer, and the development chart session, and it resolves per-frame input into menu actions,
diagnostics intents, and gameplay-session control. The window, render device, resource loading,
and the frame loop stay outside it (in RockHeroGame and SDL3Application), so Game holds no OS window, no bgfx
device, and no pacing state — only the content and the per-frame content state derived from the
playback clock.

The render device is supplied per frame rather than owned, and the gameplay session is injected
non-owning and must outlive the Game. Move-only members (the highway renderer) and registration-free
content make this a plain object with no factory: construction cannot fail because the fallible
render-resource loading is done by the caller, which hands in an already-built renderer.
*/
class Game
{
public:
    /*!
    \brief Composition inputs for one Game: run options plus injected collaborators.

    Mirrors the surviving fields of the old GameShellOptions bag that describe content rather than
    the loop: the dev flags, the optional development package, the workspace the gameplay session
    extracts into, the injected session, and the optional scanned library that opens the menu.
    */
    struct Config
    {
        /*! \brief True activates the dev-diagnostics layer (overlay, hot-reload, section seeks). */
        bool dev_mode{false};

        /*! \brief True mirrors the highway for left-handed display. */
        bool lefty{false};

        /*!
        \brief When set, loads this package at startup and scrolls its first charted arrangement.

        The development direct-load path; empty means the menu (when a \ref library is present) or
        an empty board drives the display instead.
        */
        std::optional<std::filesystem::path> dev_package;

        /*! \brief Session-private scratch directory the gameplay session extracts the package into. */
        std::filesystem::path session_workspace_directory;

        /*!
        \brief Non-owning gameplay session composed by the app; null runs without audio.

        The caller owns lifetime: the session must outlive the Game and be closed by the caller.
        */
        core::GameplaySession* gameplay_session{nullptr};

        /*!
        \brief When set, opens the song-selection menu over this scanned library.

        Empty, or with \ref dev_package set, skips the menu and runs the direct-load path.
        */
        std::optional<core::LibraryIndex> library;
    };

    /*!
    \brief Builds the content: renderer, diagnostics, and the initial dev/menu state.

    Constructs the diagnostics controller for \p config .dev_mode, and — when a dev package is
    given — loads its chart into the renderer and starts the injected gameplay session on it. When a
    library is given instead, installs the SDL keycode menu bindings (the concrete keycodes live
    here, at the game/ui composition boundary, so game/core stays windowing-free) and opens the
    menu. No behavior is deferred to a separate init step.

    \param renderer Already-built highway renderer this content takes ownership of.
    \param config Run options and injected collaborators; see \ref Config.
    */
    Game(common::ui::HighwayRenderer renderer, Config config);

    Game(const Game&) = delete;
    Game& operator=(const Game&) = delete;
    Game(Game&&) = delete;
    Game& operator=(Game&&) = delete;
    ~Game() = default;

    /*!
    \brief Resolves this frame's input into menu, diagnostics, and session actions.

    Applies the menu-vs-gameplay switch: in menu mode raw keycodes resolve through the bindings into
    menu navigation, and a confirmed pick launches the song; in gameplay mode Escape returns to the
    menu and the mapped game keys drive the diagnostics toggles, section seeks, and play/pause and
    restart on the gameplay session. Quit and resize are the caller's (loop/surface) concern and are
    not consumed here.

    \param events Frame-relevant window events for this frame.
    */
    void handleWindowEvents(const GameWindowEvents& events);

    /*!
    \brief Advances the content for this frame's clock time and returns its timing readout.

    Runs any queued diagnostics side effects, polls the dev chart source for a settled reload, reads
    the active playback clock (the gameplay session's when present, else the dev session's stand-in),
    and samples the frame clock into render-ready song time. Stores the resulting per-frame content
    state (song time, snapshot, frame sample) that \ref render draws from.

    \param monotonic_now Loop-thread steady-clock timestamp anchoring this frame.
    \return This frame's timing readout, for the caller's frame instrumentation.
    */
    [[nodiscard]] core::FrameClockSample update(std::chrono::nanoseconds monotonic_now);

    /*!
    \brief Draws this frame: the highway plus the menu or the diagnostics overlay.

    Uses the content state set by the most recent \ref update. Draws the highway scene, then either
    the song-selection menu (in menu mode) or, in gameplay mode, the diagnostics overlay and its
    readouts when the overlay is visible.

    \param device Render device to draw into for this frame.
    \param pacing_summary Latest frame-pacing summary for the overlay's timing readout, when one has
    closed a window; empty until the first summary window closes.
    */
    void render(
        common::ui::RenderDevice& device,
        const std::optional<core::FramePacingSummary>& pacing_summary);

private:
    // Starts the picked song: reprojects the chart into the renderer and (re)starts the gameplay
    // session on it, closing any prior session first so a second pick reloads cleanly.
    void launchSong(const core::SongSelectLaunch& launch);

    // Content owned by this object.
    common::ui::HighwayRenderer m_renderer;
    DiagnosticsOverlay m_overlay;
    core::DiagnosticsController m_diagnostics;
    core::FrameClock m_frame_clock;
    std::optional<DevSession> m_dev_session;
    std::optional<core::SongSelectMenu> m_menu;
    core::MenuBindings m_menu_bindings;
    bool m_in_menu{false};

    // Injected collaborator (non-owning) and the run options the content needs after construction.
    core::GameplaySession* m_session{nullptr};
    bool m_dev_mode{false};
    bool m_lefty{false};
    std::filesystem::path m_session_workspace_directory;

    // Per-frame content state produced by update() and consumed by render().
    common::audio::PlaybackClockSnapshot m_snapshot{};
    core::FrameClockSample m_frame_sample{};
    double m_last_song_seconds{0.0};
};

} // namespace rock_hero::game::ui
