#include "game/game.h"

#include "dev/dev_session.h"
#include "surface/game_window.h"

#include <SDL3/SDL.h>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <format>
#include <optional>
#include <print>
#include <rock_hero/common/audio/clock/playback_clock_snapshot.h>
#include <rock_hero/common/core/shared/logger.h>
#include <rock_hero/common/core/song/arrangement.h>
#include <rock_hero/common/ui/highway/highway_renderer.h>
#include <rock_hero/common/ui/render/render_device.h>
#include <rock_hero/game/core/diagnostics/diagnostics.h>
#include <rock_hero/game/core/input/menu_action.h>
#include <rock_hero/game/core/input/menu_input_trigger.h>
#include <rock_hero/game/core/library/library_index.h>
#include <rock_hero/game/core/menu/song_select_menu.h>
#include <rock_hero/game/core/session/gameplay_session.h>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace rock_hero::game::ui
{

namespace
{

// Typed-overload executor for the diagnostics layer's requested side effects; Game owns the
// session and renderer, so intent execution lives here rather than in the headless controller.
struct DiagnosticsIntentExecutor
{
    std::optional<DevSession>& dev_session;
    core::GameplaySession* gameplay_session;
    common::ui::HighwayRenderer& renderer;
    std::chrono::nanoseconds now;

    void operator()(const core::ReloadChartIntent& /*intent*/) const
    {
        if (dev_session.has_value())
        {
            std::optional<common::core::HighwayViewState> state = dev_session->reload(now);
            if (state.has_value())
            {
                renderer.setViewState(std::move(*state));
            }
        }
    }

    void operator()(const core::SeekToSectionIntent& intent) const
    {
        if (!dev_session.has_value())
        {
            return;
        }

        // With a live gameplay session the engine transport owns song time, so a section seek
        // moves the transport; the stand-in clock re-anchor covers clock-only dev runs.
        if (gameplay_session != nullptr)
        {
            const std::optional<double> target =
                dev_session->sectionStartSeconds(intent.section_index);
            if (target.has_value())
            {
                if (const auto sought = gameplay_session->seek(common::core::TimePosition{*target});
                    !sought.has_value())
                {
                    RH_LOG_WARNING(
                        "game.session", "section seek refused: {}", sought.error().message);
                }
            }
            return;
        }

        dev_session->seekToSection(intent.section_index, now);
    }
};

// Renders the song-selection menu with the bgfx debug font over the board backdrop (functional
// placeholder styling; plan 26 defers real menu art). Both a '>' marker and a translucent overlay
// bar mark the highlighted row; character row R sits at pixel y = R * 16 (the debug font cell).
void renderMenu(
    common::ui::RenderDevice& device, common::ui::HighwayRenderer& renderer,
    const core::SongSelectMenu& menu)
{
    constexpr std::uint16_t first_row = 3;
    constexpr std::uint16_t left_column = 4;
    constexpr float cell_height_pixels = 16.0F;

    device.setDebugTextEnabled(true);
    device.clearDebugText();

    std::size_t item_count = 0;
    std::size_t selected = 0;

    if (menu.screen() == core::SongSelectScreen::SongList)
    {
        device.printDebugText(2, 1, "ROCK HERO   -   SELECT A SONG");
        const std::vector<core::LibraryEntry>& entries = menu.library().entries;
        item_count = entries.size();
        selected = menu.selectedSongIndex();
        if (entries.empty())
        {
            device.printDebugText(
                left_column,
                first_row,
                "No songs found. Drop .rock files into your Songs folder and restart.");
        }
        for (std::size_t i = 0; i < entries.size(); ++i)
        {
            const core::LibraryEntry& entry = entries[i];
            const std::string_view title = entry.metadata.title.empty()
                                               ? std::string_view{"(untitled)"}
                                               : std::string_view{entry.metadata.title};
            const std::string line = entry.metadata.artist.empty()
                                         ? std::format("{} {}", i == selected ? '>' : ' ', title)
                                         : std::format(
                                               "{} {}   -   {}",
                                               i == selected ? '>' : ' ',
                                               title,
                                               entry.metadata.artist);
            device.printDebugText(left_column, static_cast<std::uint16_t>(first_row + i), line);
        }
    }
    else
    {
        const core::LibraryEntry* const song = menu.currentSong();
        device.printDebugText(
            2,
            1,
            std::format(
                "ARRANGEMENTS   -   {}", song != nullptr ? song->metadata.title : std::string{}));
        const std::span<const core::LibraryArrangementSummary> arrangements =
            menu.currentArrangements();
        item_count = arrangements.size();
        selected = menu.selectedArrangementIndex();
        for (std::size_t i = 0; i < arrangements.size(); ++i)
        {
            const auto& arrangement = arrangements[i];
            const std::string_view part = arrangement.part.has_value()
                                              ? common::core::partToken(*arrangement.part)
                                              : std::string_view{"Arrangement"};
            device.printDebugText(
                left_column,
                static_cast<std::uint16_t>(first_row + i),
                std::format("{} {}", i == selected ? '>' : ' ', part));
        }
    }

    device.printDebugText(
        2,
        static_cast<std::uint16_t>(first_row + item_count + 2),
        menu.screen() == core::SongSelectScreen::SongList ? "Up/Down move   Enter choose"
                                                          : "Up/Down move   Enter play   Esc back");

    if (item_count > 0)
    {
        const float top = static_cast<float>(first_row + selected) * cell_height_pixels;
        const common::ui::HighwayOverlayRect bar{
            .left = 0.0F,
            .top = top,
            .right = static_cast<float>(device.width()),
            .bottom = top + cell_height_pixels,
            .abgr = 0x40FFFFFFU,
        };
        renderer.drawOverlayRects(
            std::span<const common::ui::HighwayOverlayRect>{&bar, 1},
            device.width(),
            device.height());
    }
}

} // namespace

Game::Game(common::ui::HighwayRenderer renderer, Config config)
    : m_renderer(std::move(renderer))
    , m_diagnostics(config.dev_mode)
    , m_session(config.gameplay_session)
    , m_dev_mode(config.dev_mode)
    , m_lefty(config.lefty)
    , m_session_workspace_directory(std::move(config.session_workspace_directory))
{
    // Dev fixture path: load the requested package's first charted arrangement for the highway
    // display, sections, and hot-reload. With a gameplay session injected, the fixture's
    // stand-in clock goes unused — the engine's real playback clock drives song time instead.
    if (config.dev_package.has_value())
    {
        m_dev_session = DevSession::create(
            *config.dev_package, m_lefty, std::chrono::steady_clock::now().time_since_epoch());
        if (m_dev_session.has_value())
        {
            std::optional<common::core::HighwayViewState> dev_state =
                m_dev_session->takeLoadedViewState();
            if (dev_state.has_value())
            {
                m_renderer.setViewState(std::move(*dev_state));
            }
        }

        // Milestone-0 audio path (plan 21 Phase 6): the app-composed session loads the same package
        // for real playback — backing track, live tone rig, scheduled tone switching. A load failure
        // is reported and the content keeps rendering (the chart still displays); the missing-plugin
        // refusal (21-Q1) surfaces here with its full install list.
        if (m_session != nullptr)
        {
            const auto started = m_session->start(
                core::GameplaySessionRequest{
                    .package_path = *config.dev_package,
                    // The audible rig must belong to the arrangement on screen; empty falls back to
                    // the song's first arrangement when the fixture found no charted one.
                    .arrangement_id = m_dev_session.has_value()
                                          ? m_dev_session->chosenArrangementId()
                                          : std::string{},
                    .workspace_directory = m_session_workspace_directory,
                });
            if (!started.has_value())
            {
                RH_LOG_WARNING("game.session", "session start failed: {}", started.error().message);
                std::println(stderr, "rock-hero: {}", started.error().message);
            }
            else
            {
                RH_LOG_INFO(
                    "game.session",
                    "session loading package={:?} (space starts playback once ready)",
                    config.dev_package->string());
            }
        }
    }

    // Song-selection menu (plan 26 Phases 5-7): when app/ composed a scanned library, Game opens the
    // menu and starts the session from the player's pick rather than auto-loading a dev package.
    // Keyboard triggers resolve through the Phase-5 bindings; the concrete SDL keycode defaults are
    // installed here at the composition boundary so game/core stays SDL-free. PauseMenu and Rescan
    // stay unbound until their consumers exist — the in-song pause menu (plan 27) and the main
    // menu's rescan entry (plan 26 Phase 7); a default binding now would map keys to no-ops.
    if (config.library.has_value())
    {
        m_menu.emplace(std::move(*config.library));
        const auto key_trigger = [](const SDL_Keycode code) {
            return core::MenuInputTrigger{
                .source = core::MenuInputSource::Keyboard, .code = static_cast<int>(code)
            };
        };
        m_menu_bindings.bind(core::MenuAction::NavigateUp, key_trigger(SDLK_UP));
        m_menu_bindings.bind(core::MenuAction::NavigateDown, key_trigger(SDLK_DOWN));
        m_menu_bindings.bind(core::MenuAction::NavigateLeft, key_trigger(SDLK_LEFT));
        m_menu_bindings.bind(core::MenuAction::NavigateRight, key_trigger(SDLK_RIGHT));
        m_menu_bindings.bind(core::MenuAction::Accept, key_trigger(SDLK_RETURN));
        m_menu_bindings.bind(core::MenuAction::Back, key_trigger(SDLK_ESCAPE));
        m_in_menu = true;
    }
}

void Game::launchSong(const core::SongSelectLaunch& launch)
{
    // Load the display chart first, into a local. A pick that produces no loadable chart must not
    // drop the player out of the menu onto an empty board, so bail before touching the session,
    // the renderer, or the menu flag.
    std::optional<DevSession> dev_session = DevSession::create(
        launch.package_path, m_lefty, std::chrono::steady_clock::now().time_since_epoch());
    if (!dev_session.has_value())
    {
        RH_LOG_WARNING(
            "game.session",
            "launch aborted: package has no loadable chart package={:?}",
            launch.package_path.string());
        std::println(
            stderr, "rock-hero: package has no loadable chart: {}", launch.package_path.string());
        return;
    }

    // Start the gameplay session (audio) before leaving the menu, so a synchronous start failure
    // keeps the player in the menu rather than on a silent, frozen board. Closing any prior session
    // first lets a second pick reload cleanly and satisfies start()'s Idle precondition.
    if (m_session != nullptr)
    {
        m_session->close();
        if (const auto started = m_session->start(
                core::GameplaySessionRequest{
                    .package_path = launch.package_path,
                    .arrangement_id = launch.arrangement_id,
                    .workspace_directory = m_session_workspace_directory,
                });
            !started.has_value())
        {
            RH_LOG_WARNING("game.session", "session start failed: {}", started.error().message);
            std::println(stderr, "rock-hero: {}", started.error().message);
            return;
        }
    }

    // Both steps succeeded: adopt the loaded chart and leave the menu for the gameplay surface.
    std::optional<common::core::HighwayViewState> state = dev_session->takeLoadedViewState();
    if (state.has_value())
    {
        m_renderer.setViewState(std::move(*state));
    }
    m_dev_session = std::move(dev_session);
    m_in_menu = false;
}

void Game::handleWindowEvents(const GameWindowEvents& events)
{
    if (m_in_menu && m_menu.has_value())
    {
        for (const int code : events.key_codes_pressed)
        {
            if (const std::optional<core::MenuAction> action = m_menu_bindings.resolve(
                    core::MenuInputTrigger{
                        .source = core::MenuInputSource::Keyboard, .code = code
                    });
                action.has_value())
            {
                m_menu->handle(*action);
            }
        }
        if (std::optional<core::SongSelectLaunch> launch = m_menu->takeLaunch(); launch.has_value())
        {
            launchSong(*launch);
        }
        return;
    }

    // Esc leaves the current song and returns to the menu (when a library was scanned).
    for (const int code : events.key_codes_pressed)
    {
        if (m_menu.has_value() && std::cmp_equal(code, SDLK_ESCAPE))
        {
            if (m_session != nullptr)
            {
                m_session->close();
            }
            m_dev_session.reset();
            m_in_menu = true;
        }
    }
    for (const GameKey key : events.keys_pressed)
    {
        switch (key)
        {
            case GameKey::ToggleDiagnosticsOverlay:
            {
                m_diagnostics.toggleOverlay();
                break;
            }
            case GameKey::ToggleAutoplay:
            {
                m_diagnostics.toggleAutoplay();
                break;
            }
            case GameKey::ReloadChart:
            {
                m_diagnostics.requestChartReload();
                break;
            }
            case GameKey::SeekPreviousSection:
            {
                if (m_dev_session.has_value())
                {
                    const std::optional<std::size_t> section =
                        m_dev_session->sectionBefore(m_last_song_seconds);
                    if (section.has_value())
                    {
                        m_diagnostics.requestSeekToSection(*section);
                    }
                }
                break;
            }
            case GameKey::SeekNextSection:
            {
                if (m_dev_session.has_value())
                {
                    const std::optional<std::size_t> section =
                        m_dev_session->sectionAfter(m_last_song_seconds);
                    if (section.has_value())
                    {
                        m_diagnostics.requestSeekToSection(*section);
                    }
                }
                break;
            }
            case GameKey::TogglePlayPause:
            {
                if (m_session != nullptr)
                {
                    // Play covers Ready, Paused, and Finished (restart); pause covers Playing.
                    // Out-of-stage presses (still loading, failed) are refused by the session and
                    // logged rather than crashing or silently retrying.
                    const bool playing = m_session->stage() == core::GameplaySessionStage::Playing;
                    const auto toggled = playing ? m_session->pause() : m_session->play();
                    if (!toggled.has_value())
                    {
                        RH_LOG_WARNING(
                            "game.session", "play/pause refused: {}", toggled.error().message);
                    }
                }
                break;
            }
            case GameKey::RestartSong:
            {
                if (m_session != nullptr)
                {
                    if (const auto restarted = m_session->restart(); !restarted.has_value())
                    {
                        RH_LOG_WARNING(
                            "game.session", "restart refused: {}", restarted.error().message);
                    }
                }
                break;
            }
        }
    }
}

core::FrameClockSample Game::update(const std::chrono::nanoseconds monotonic_now)
{
    // Requested diagnostics side effects run at the frame stamp, before content is built, so a
    // reload or seek is visible in the same frame that acknowledged it.
    for (const core::DiagnosticsIntent& intent : m_diagnostics.takePendingIntents())
    {
        std::visit(
            DiagnosticsIntentExecutor{
                .dev_session = m_dev_session,
                .gameplay_session = m_session,
                .renderer = m_renderer,
                .now = monotonic_now,
            },
            intent);
    }

    // Chart hot-reload: settled on-disk edits reproject into the renderer (dev mode only — the
    // watcher polls nothing without a dev session, and players run without dev mode).
    if (m_dev_mode && m_dev_session.has_value())
    {
        std::optional<common::core::HighwayViewState> reloaded =
            m_dev_session->pollForReload(monotonic_now);
        if (reloaded.has_value())
        {
            m_renderer.setViewState(std::move(*reloaded));
        }
    }

    m_snapshot = common::audio::PlaybackClockSnapshot{};
    if (m_session != nullptr)
    {
        m_snapshot = m_session->clock().snapshot();
    }
    else if (m_dev_session.has_value())
    {
        m_snapshot = m_dev_session->clockSnapshotAt(monotonic_now);
    }
    m_frame_sample = m_frame_clock.sample(m_snapshot, monotonic_now);
    m_last_song_seconds = m_frame_sample.song_time.seconds;
    return m_frame_sample;
}

void Game::render(
    common::ui::RenderDevice& device, const std::optional<core::FramePacingSummary>& pacing_summary)
{
    m_renderer.draw(
        m_frame_sample.song_time.seconds,
        static_cast<double>(m_frame_sample.frame_delta.count()) / 1.0e9,
        device.width(),
        device.height());

    if (m_in_menu && m_menu.has_value())
    {
        renderMenu(device, m_renderer, *m_menu);
        return;
    }

    // Diagnostics overlay: the frame-time graph plus the debug-text readouts.
    const bool overlay_visible = m_diagnostics.state().overlay_visible;
    device.setDebugTextEnabled(overlay_visible);
    m_overlay.recordFrameDelta(m_frame_sample.frame_delta);
    if (overlay_visible)
    {
        m_renderer.drawOverlayRects(m_overlay.buildRects(), device.width(), device.height());
    }
    device.clearDebugText();
    if (!overlay_visible)
    {
        return;
    }
    if (pacing_summary.has_value())
    {
        device.printDebugText(
            1,
            1,
            std::format(
                "frame avg {:.2f} ms  max {:.2f} ms  ({} fps)",
                static_cast<double>(pacing_summary->average_delta.count()) / 1.0e6,
                static_cast<double>(pacing_summary->max_delta.count()) / 1.0e6,
                pacing_summary->frame_count));
    }
    // Clock panel: song time, mirror age, and the extrapolation drift (rendered song time minus the
    // raw snapshot position — how far ahead of the mirror the frame ran).
    device.printDebugText(
        1,
        2,
        std::format(
            "song {:.3f} s  mirror {}  drift {:+.2f} ms  {}",
            m_frame_sample.song_time.seconds,
            m_frame_sample.snapshot_age.has_value()
                ? std::format(
                      "{:.2f} ms",
                      static_cast<double>(m_frame_sample.snapshot_age->count()) / 1.0e6)
                : std::string{"unpublished"},
            (m_frame_sample.song_time.seconds - m_snapshot.position.seconds) * 1.0e3,
            m_frame_sample.playing ? "playing" : "stopped"));
    device.printDebugText(
        1,
        3,
        std::format(
            "F1 overlay  F2 autoplay{}  F5 reload  PgUp/PgDn section",
            m_diagnostics.state().autoplay_enabled ? " [AUTOPLAY]" : ""));
}

} // namespace rock_hero::game::ui
