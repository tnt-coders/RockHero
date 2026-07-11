#include "surface/game_shell.h"

#include "highway/highway_renderer.h"
#include "surface/game_window.h"
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
#include <rock_hero/common/core/highway/highway_projection.h>
#include <rock_hero/common/core/highway/highway_view_state.h>
#include <rock_hero/common/core/package/rock_song_package.h>
#include <rock_hero/common/core/shared/logger.h>
#include <rock_hero/common/core/song/song.h>
#include <rock_hero/game/core/frame_clock/frame_clock.h>
#include <rock_hero/game/core/resources/game_resources.h>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

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

// Loads the dev package's first charted arrangement into a highway view state (plan 25 Phase
// 3's fixture path; plan 26's library replaces this for players, plan 21's session for real
// playback). Archives extract into a scratch workspace that is removed immediately after the
// read — the projected view state is fully materialized in memory.
[[nodiscard]] std::optional<common::core::HighwayViewState> loadDevViewState(
    const std::filesystem::path& package_path, const bool lefty)
{
    // Editor project packages (.rhp) wrap the song content in a song/ subdirectory beside
    // project.json; bare song packages carry song.json at the root. Accept both.
    const auto song_content_root = [](const std::filesystem::path& root) {
        std::error_code probe_error;
        return std::filesystem::exists(root / "song" / "song.json", probe_error) ? root / "song"
                                                                                 : root;
    };

    const auto read_song =
        [&]() -> std::expected<common::core::Song, common::core::SongPackageError> {
        std::error_code probe_error;
        if (std::filesystem::is_directory(package_path, probe_error))
        {
            return common::core::readRockSongPackageDirectory(song_content_root(package_path));
        }
        const std::filesystem::path workspace =
            std::filesystem::temp_directory_path() /
            ("rock-hero-dev-" +
             std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        std::filesystem::create_directories(workspace);
        auto extracted = common::core::readRockSongPackage(package_path, workspace);
        if (!extracted.has_value() &&
            std::filesystem::exists(workspace / "song" / "song.json", probe_error))
        {
            extracted = common::core::readRockSongPackageDirectory(workspace / "song");
        }
        std::error_code cleanup_error;
        std::filesystem::remove_all(workspace, cleanup_error);
        return extracted;
    };
    const std::expected<common::core::Song, common::core::SongPackageError> song = read_song();

    if (!song.has_value())
    {
        std::println(stderr, "rock-hero: dev package load failed: {}", song.error().message);
        return std::nullopt;
    }

    for (const common::core::Arrangement& arrangement : song->arrangements)
    {
        if (!arrangement.chart.has_value())
        {
            continue;
        }
        // Lowest-pitched string on top is the 3D notation's default (user decision 2026-07-11,
        // recorded in plan 25); the shared projection's invert flag realizes it, and plans 26/27
        // surface the per-player setting later.
        common::core::HighwayViewState state = common::core::makeHighwayViewState(
            arrangement,
            song->tempo_map,
            common::core::HighwayDisplayOptions{.mirrored = lefty, .invert_string_order = true});
        RH_LOG_INFO(
            "game.highway",
            "dev package loaded notes={} beats={} sections={} lefty={}",
            state.notes.size(),
            state.beats.size(),
            state.sections.size(),
            lefty);
        return state;
    }

    std::println(stderr, "rock-hero: dev package has no charted arrangement");
    return std::nullopt;
}

// Emits the Phase 3 timing channels: a per-frame trace record (dormant at the logger's default
// Info runtime level until the dev-diagnostics flag raises verbosity — plan 20 Phase 4) and a
// once-per-second pacing summary at Info so steady-state logs stay readable. mirror_age_ns
// logs -1 while the playback clock has never published. Returns the pacing summary when this
// frame closed a window so the overlay can display it.
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
        });
    if (!device.has_value())
    {
        std::println(stderr, "rock-hero: {}", device.error().message);
        return 1;
    }

    // The highway renderer owns every scene-side bgfx handle; declared after the device so it is
    // destroyed first (all handles die before bgfx::shutdown — the wrapper's ordering contract).
    std::optional<core::GameResources> resources = makeGameResources();
    if (!resources.has_value())
    {
        return 1;
    }
    std::expected<HighwayRenderer, HighwayRendererError> renderer =
        HighwayRenderer::create(*resources);
    if (!renderer.has_value())
    {
        std::println(stderr, "rock-hero: {}", renderer.error().message);
        return 1;
    }

    // Dev fixture path: load the requested package's first charted arrangement and scroll it
    // against a development clock publisher (plan 21's engine replaces both).
    bool dev_clock_active = false;
    if (options.dev_package.has_value())
    {
        std::optional<common::core::HighwayViewState> dev_state =
            loadDevViewState(*options.dev_package, options.lefty);
        if (dev_state.has_value())
        {
            renderer->setViewState(std::move(*dev_state));
            dev_clock_active = true;
        }
    }

    device->setDebugTextEnabled(true);

    // The L2 frame loop: freshest input first, JUCE callbacks settled before the frame is built,
    // then the vsynced present paces the whole loop.
    core::FrameClock frame_clock;
    core::FramePacingStats pacing_stats;
    std::optional<core::FramePacingSummary> last_pacing_summary;
    std::chrono::nanoseconds previous_frame_boundary{0};
    const std::chrono::nanoseconds dev_clock_start =
        std::chrono::steady_clock::now().time_since_epoch();
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

        drainPendingJuceMessages(g_max_juce_messages_per_frame);

        // One steady-clock stamp at the start of frame building anchors everything this frame
        // draws, and the clock snapshot is read once so every drawable shares one coherent song
        // time. No engine lives in the game process yet — plan 21 (G21-TRACKTION-GO, closed)
        // composes the real IPlaybackClock — so the loop consumes either an unpublished snapshot
        // or, in dev-package mode, a locally published stand-in that plays from process start.
        // The sanctioned song-time path stays live end to end; song time never comes from wall
        // clock or frame counts inside the loop (architecture.md "Timing and Latency").
        const std::chrono::nanoseconds frame_sample_time =
            std::chrono::steady_clock::now().time_since_epoch();
        common::audio::PlaybackClockSnapshot snapshot{};
        if (dev_clock_active)
        {
            snapshot = common::audio::PlaybackClockSnapshot{
                .position =
                    common::core::TimePosition{
                        static_cast<double>((frame_sample_time - dev_clock_start).count()) / 1.0e9
                    },
                .monotonic_capture_time = frame_sample_time,
                .playback_rate = 1.0,
                .playing = true,
            };
        }
        const core::FrameClockSample frame_sample = frame_clock.sample(snapshot, frame_sample_time);

        renderer->draw(
            frame_sample.song_time.seconds,
            static_cast<double>(frame_sample.frame_delta.count()) / 1.0e9,
            device->width(),
            device->height());

        // Overlay v1: frame pacing and playback-clock drift readouts over the scene.
        device->clearDebugText();
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
        device->printDebugText(
            1,
            2,
            std::format(
                "song {:.3f} s  mirror {}  {}",
                frame_sample.song_time.seconds,
                frame_sample.snapshot_age.has_value()
                    ? std::format(
                          "{:.2f} ms",
                          static_cast<double>(frame_sample.snapshot_age->count()) / 1.0e6)
                    : std::string{"unpublished"},
                frame_sample.playing ? "playing" : "stopped"));

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
