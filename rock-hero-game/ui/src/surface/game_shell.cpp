#include "surface/game_shell.h"

#include "dev/dev_session.h"
#include "overlay/diagnostics_overlay.h"
#include "surface/game_window.h"
#include "surface/highway_shader_loader.h"
#include "surface/juce_message_pump.h"
#include "surface/render_device.h"

#include <SDL3/SDL.h>
#include <chrono>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <format>
#include <juce_events/juce_events.h>
#include <optional>
#include <print>
#include <rock_hero/common/audio/clock/playback_clock_snapshot.h>
#include <rock_hero/common/core/shared/logger.h>
#include <rock_hero/common/ui/highway/highway_renderer.h>
#include <rock_hero/game/core/diagnostics/diagnostics.h>
#include <rock_hero/game/core/frame_clock/frame_clock.h>
#include <rock_hero/game/core/resources/game_resources.h>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace rock_hero::game::ui
{

namespace
{

// Initial window size in logical coordinates; plan 26's video settings will make this a choice.
constexpr std::uint32_t g_initial_window_width = 1280;
constexpr std::uint32_t g_initial_window_height = 720;

// Per-frame JUCE dispatch bound. The gate soak measured a real-world per-frame maximum of 3
// messages; this is a safety valve against a pathological self-posting burst, not a tuned value.
constexpr int g_max_juce_messages_per_frame = 256;

// Resolves the deployed resource-pack root next to the executable — the one loading seam
// packaged assets come through (plan 20 Phase 2).
[[nodiscard]] std::optional<core::GameResources> makeGameResources()
{
    // SDL documents the base path as UTF-8 and as null on failure: the null must be caught before
    // path construction (UB), and the bytes must be decoded as UTF-8 explicitly — MSVC's narrow
    // std::filesystem::path constructor decodes via the system codepage, which corrupts
    // non-ASCII install paths.
    const char* base_path = SDL_GetBasePath();
    if (base_path == nullptr)
    {
        std::println(stderr, "rock-hero: SDL_GetBasePath failed: {}", SDL_GetError());
        return std::nullopt;
    }
    const std::string_view base_path_bytes{base_path};
    const std::u8string base_path_utf8(base_path_bytes.begin(), base_path_bytes.end());
    const std::filesystem::path resources_root =
        std::filesystem::path{base_path_utf8} / "resources";

    std::expected<core::GameResources, core::GameResourcesError> resources =
        core::GameResources::create(resources_root);
    if (!resources.has_value())
    {
        std::println(stderr, "rock-hero: {}", resources.error().message);
        return std::nullopt;
    }
    return std::move(*resources);
}

// Emits the Phase 3 timing channels: a per-frame trace record (dormant at the logger's default
// Info runtime level; the --dev flag lowers the level to Trace) and a once-per-second pacing
// summary at Info so steady-state logs stay readable. mirror_age_ns logs -1 while the playback
// clock has never published. Returns the pacing summary when this frame closed a window so the
// overlay can display it.
std::optional<core::FramePacingSummary> logFrameInstrumentation(
    const RenderDevice& device, const core::FrameClockSample& frame_sample,
    const std::chrono::nanoseconds frame_boundary_time,
    const std::chrono::nanoseconds previous_frame_boundary, core::FramePacingStats& pacing_stats)
{
    const std::chrono::nanoseconds boundary_delta =
        previous_frame_boundary == std::chrono::nanoseconds{0}
            ? std::chrono::nanoseconds{0}
            : frame_boundary_time - previous_frame_boundary;
    const std::int64_t mirror_age_ns = frame_sample.snapshot_age.has_value()
                                           ? frame_sample.snapshot_age->count()
                                           : std::int64_t{-1};

    RH_LOG_TRACE(
        "game.frame",
        "frame boundary_ns={} delta_ns={} bgfx_cpu_ns={} song_time_s={} mirror_age_ns={} "
        "playing={}",
        frame_boundary_time.count(),
        boundary_delta.count(),
        device.lastCpuFrameTime().count(),
        frame_sample.song_time.seconds,
        mirror_age_ns,
        frame_sample.playing);

    const std::optional<core::FramePacingSummary> summary = pacing_stats.record(boundary_delta);
    if (summary.has_value())
    {
        RH_LOG_INFO(
            "game.frame",
            "pacing frames={} window_ms={} avg_ns={} min_ns={} max_ns={} mirror_age_ns={}",
            summary->frame_count,
            std::chrono::duration_cast<std::chrono::milliseconds>(summary->window_duration).count(),
            summary->average_delta.count(),
            summary->min_delta.count(),
            summary->max_delta.count(),
            mirror_age_ns);
    }
    return summary;
}

// Typed-overload executor for the diagnostics layer's requested side effects; the shell owns the
// session and renderer, so intent execution lives here rather than in the headless controller.
struct DiagnosticsIntentExecutor
{
    std::optional<DevSession>& dev_session;
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
        if (dev_session.has_value())
        {
            dev_session->seekToSection(intent.section_index, now);
        }
    }
};

} // namespace

// Composes the L2 loop. Declaration order is the teardown contract, in reverse: the JUCE runtime
// outlives the window and device (JUCE shutdown must come last, after every JUCE-dependent
// object is gone), and the device dies before the window it renders into. When the audio engine
// joins this shell (plan 21), it must be stopped, its live rig cleared, and the engine destroyed
// before the device/window teardown below — a live plugin editor window must never be destroyed
// after the windowing/GPU stack is gone.
int GameShell::run(const GameShellOptions& options)
{
    // Binds this thread as the JUCE message thread for the process lifetime; everything JUCE
    // (Tracktion timers, async callbacks, plugin windows) is serviced by the per-frame drain.
    const juce::ScopedJuceInitialiser_GUI juce_runtime;

    std::expected<GameWindow, GameWindowError> window = GameWindow::create(
        "Rock Hero", PixelSize{.width = g_initial_window_width, .height = g_initial_window_height});
    if (!window.has_value())
    {
        // The logging backend is not composed until the instrumentation phase, so startup
        // failures report on stderr (visible when launched from a terminal) plus the exit code.
        std::println(stderr, "rock-hero: {}", window.error().message);
        return 1;
    }

    const PixelSize initial_size = window->pixelSize();
    std::expected<RenderDevice, RenderDeviceError> device = RenderDevice::create(
        RenderDeviceConfig{
            .backend = defaultRenderBackend(),
            .native_window_handle = window->nativeWindowHandle(),
            .width = initial_size.width,
            .height = initial_size.height,
            .vsync = true,
            .debug = options.dev_mode,
        });
    if (!device.has_value())
    {
        std::println(stderr, "rock-hero: {}", device.error().message);
        return 1;
    }

    // Scene renderers own bgfx handles, so they are declared after the device and destroyed
    // first (all handles die before bgfx::shutdown — the wrapper's ordering contract).
    std::optional<core::GameResources> resources = makeGameResources();
    if (!resources.has_value())
    {
        return 1;
    }
    const std::expected<common::ui::HighwayShaderSet, core::GameResourcesError> highway_shaders =
        loadHighwayShaderSet(*resources);
    if (!highway_shaders.has_value())
    {
        std::println(stderr, "rock-hero: {}", highway_shaders.error().message);
        return 1;
    }
    std::expected<common::ui::HighwayRenderer, common::ui::HighwayRendererError> renderer =
        common::ui::HighwayRenderer::create(*highway_shaders);
    if (!renderer.has_value())
    {
        std::println(stderr, "rock-hero: {}", renderer.error().message);
        return 1;
    }
    DiagnosticsOverlay overlay;

    // Dev-diagnostics layer (plan 20 Phase 4): compiled into every build, active only behind the
    // runtime flag; every mutation is gated inside the controller.
    core::DiagnosticsController diagnostics{options.dev_mode};

    // Dev fixture path: load the requested package's first charted arrangement and scroll it
    // against the session's stand-in clock (plan 21's engine replaces both).
    std::optional<DevSession> dev_session;
    if (options.dev_package.has_value())
    {
        dev_session = DevSession::create(
            *options.dev_package,
            options.lefty,
            std::chrono::steady_clock::now().time_since_epoch());
        if (dev_session.has_value())
        {
            std::optional<common::core::HighwayViewState> dev_state =
                dev_session->takeLoadedViewState();
            if (dev_state.has_value())
            {
                renderer->setViewState(std::move(*dev_state));
            }
        }
    }

    // The L2 frame loop: freshest input first, JUCE callbacks settled before the frame is built,
    // then the vsynced present paces the whole loop.
    core::FrameClock frame_clock;
    core::FramePacingStats pacing_stats;
    std::optional<core::FramePacingSummary> last_pacing_summary;
    std::chrono::nanoseconds previous_frame_boundary{0};
    double last_song_seconds = 0.0;
    std::uint64_t frames_submitted = 0;
    while (true)
    {
        const GameWindowEvents events = window->pollEvents();
        if (events.quit_requested)
        {
            break;
        }
        if (events.pixel_size_changed.has_value())
        {
            device->resize(events.pixel_size_changed->width, events.pixel_size_changed->height);
        }
        for (const GameKey key : events.keys_pressed)
        {
            switch (key)
            {
                case GameKey::ToggleDiagnosticsOverlay:
                {
                    diagnostics.toggleOverlay();
                    break;
                }
                case GameKey::ToggleAutoplay:
                {
                    diagnostics.toggleAutoplay();
                    break;
                }
                case GameKey::ReloadChart:
                {
                    diagnostics.requestChartReload();
                    break;
                }
                case GameKey::SeekPreviousSection:
                {
                    if (dev_session.has_value())
                    {
                        const std::optional<std::size_t> section =
                            dev_session->sectionBefore(last_song_seconds);
                        if (section.has_value())
                        {
                            diagnostics.requestSeekToSection(*section);
                        }
                    }
                    break;
                }
                case GameKey::SeekNextSection:
                {
                    if (dev_session.has_value())
                    {
                        const std::optional<std::size_t> section =
                            dev_session->sectionAfter(last_song_seconds);
                        if (section.has_value())
                        {
                            diagnostics.requestSeekToSection(*section);
                        }
                    }
                    break;
                }
            }
        }

        drainPendingJuceMessages(g_max_juce_messages_per_frame);

        // One steady-clock stamp at the start of frame building anchors everything this frame
        // draws, and the clock snapshot is read once so every drawable shares one coherent song
        // time. No engine lives in the game process yet — plan 21 (G21-TRACKTION-GO, closed)
        // composes the real IPlaybackClock — so the loop consumes either an unpublished snapshot
        // or, in dev-package mode, the session's stand-in. The sanctioned song-time path stays
        // live end to end; song time never comes from wall clock or frame counts inside the loop
        // (architecture.md "Timing and Latency").
        const std::chrono::nanoseconds frame_sample_time =
            std::chrono::steady_clock::now().time_since_epoch();

        // Requested diagnostics side effects run at the frame stamp, before content is built, so
        // a reload or seek is visible in the same frame that acknowledged it.
        for (const core::DiagnosticsIntent& intent : diagnostics.takePendingIntents())
        {
            std::visit(
                DiagnosticsIntentExecutor{
                    .dev_session = dev_session, .renderer = *renderer, .now = frame_sample_time
                },
                intent);
        }

        // Chart hot-reload: settled on-disk edits reproject into the renderer (dev mode only —
        // the watcher polls nothing without a dev session, and players run without dev mode).
        if (options.dev_mode && dev_session.has_value())
        {
            std::optional<common::core::HighwayViewState> reloaded =
                dev_session->pollForReload(frame_sample_time);
            if (reloaded.has_value())
            {
                renderer->setViewState(std::move(*reloaded));
            }
        }

        common::audio::PlaybackClockSnapshot snapshot{};
        if (dev_session.has_value())
        {
            snapshot = dev_session->clockSnapshotAt(frame_sample_time);
        }
        const core::FrameClockSample frame_sample = frame_clock.sample(snapshot, frame_sample_time);
        last_song_seconds = frame_sample.song_time.seconds;

        renderer->draw(
            frame_sample.song_time.seconds,
            static_cast<double>(frame_sample.frame_delta.count()) / 1.0e9,
            device->width(),
            device->height());

        // Diagnostics overlay: the frame-time graph plus the debug-text readouts.
        const bool overlay_visible = diagnostics.state().overlay_visible;
        device->setDebugTextEnabled(overlay_visible);
        overlay.recordFrameDelta(frame_sample.frame_delta);
        if (overlay_visible)
        {
            renderer->drawOverlayRects(overlay.buildRects(), device->width(), device->height());
        }
        device->clearDebugText();
        if (overlay_visible)
        {
            if (last_pacing_summary.has_value())
            {
                device->printDebugText(
                    1,
                    1,
                    std::format(
                        "frame avg {:.2f} ms  max {:.2f} ms  ({} fps)",
                        static_cast<double>(last_pacing_summary->average_delta.count()) / 1.0e6,
                        static_cast<double>(last_pacing_summary->max_delta.count()) / 1.0e6,
                        last_pacing_summary->frame_count));
            }
            // Clock panel: song time, mirror age, and the extrapolation drift (rendered song
            // time minus the raw snapshot position — how far ahead of the mirror the frame ran).
            device->printDebugText(
                1,
                2,
                std::format(
                    "song {:.3f} s  mirror {}  drift {:+.2f} ms  {}",
                    frame_sample.song_time.seconds,
                    frame_sample.snapshot_age.has_value()
                        ? std::format(
                              "{:.2f} ms",
                              static_cast<double>(frame_sample.snapshot_age->count()) / 1.0e6)
                        : std::string{"unpublished"},
                    (frame_sample.song_time.seconds - snapshot.position.seconds) * 1.0e3,
                    frame_sample.playing ? "playing" : "stopped"));
            device->printDebugText(
                1,
                3,
                std::format(
                    "F1 overlay  F2 autoplay{}  F5 reload  PgUp/PgDn section",
                    diagnostics.state().autoplay_enabled ? " [AUTOPLAY]" : ""));
        }

        device->submitFrame();
        ++frames_submitted;

        // Post-frame stamp: the frame boundary that paces the loop. It is a pacing anchor, not
        // photon time — bgfx presents the PREVIOUS frame at the top of each submit, and DXGI
        // queues presents ahead; plan 13's video-offset calibration owns that quasi-constant
        // chain (magnitudes recorded in the plan-20 Phase 3 record).
        const std::chrono::nanoseconds frame_boundary_time =
            std::chrono::steady_clock::now().time_since_epoch();
        const std::optional<core::FramePacingSummary> summary = logFrameInstrumentation(
            *device, frame_sample, frame_boundary_time, previous_frame_boundary, pacing_stats);
        if (summary.has_value())
        {
            last_pacing_summary = summary;
        }
        previous_frame_boundary = frame_boundary_time;

        if (options.frame_limit.has_value() && frames_submitted >= *options.frame_limit)
        {
            break;
        }
    }

    return 0;
}

} // namespace rock_hero::game::ui
