#include "surface/game_shell.h"

#include "surface/game_window.h"
#include "surface/juce_message_pump.h"
#include "surface/render_device.h"

#include <SDL3/SDL.h>
#include <chrono>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <juce_events/juce_events.h>
#include <optional>
#include <print>
#include <rock_hero/common/audio/clock/playback_clock_snapshot.h>
#include <rock_hero/common/core/shared/logger.h>
#include <rock_hero/game/core/frame_clock/frame_clock.h>
#include <rock_hero/game/core/resources/game_resources.h>
#include <string>
#include <string_view>
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

// Maps the shell's production backend to the resource tree's shader-backend vocabulary. Noop is
// a test-only backend that never reaches the shell, so it maps to the shipped default.
[[nodiscard]] core::ShaderBackend toShaderBackend(const RenderBackend backend)
{
    switch (backend)
    {
        case RenderBackend::Direct3D11:
        case RenderBackend::Noop:
        {
            return core::ShaderBackend::Direct3D11;
        }
    }

    return core::ShaderBackend::Direct3D11;
}

// Loads the surface program's compiled shaders through the resource resolver — the one loading
// seam packaged assets come through (plan 20 Phase 2) — and hands the bytes to the device.
[[nodiscard]] bool loadSurfaceProgram(RenderDevice& device, const RenderBackend backend)
{
    // SDL documents the base path as UTF-8 and as null on failure: the null must be caught before
    // path construction (UB), and the bytes must be decoded as UTF-8 explicitly — MSVC's narrow
    // std::filesystem::path constructor decodes via the system codepage, which corrupts
    // non-ASCII install paths.
    const char* base_path = SDL_GetBasePath();
    if (base_path == nullptr)
    {
        std::println(stderr, "rock-hero: SDL_GetBasePath failed: {}", SDL_GetError());
        return false;
    }
    const std::string_view base_path_bytes{base_path};
    const std::u8string base_path_utf8(base_path_bytes.begin(), base_path_bytes.end());
    const std::filesystem::path resources_root =
        std::filesystem::path{base_path_utf8} / "resources";

    const std::expected<core::GameResources, core::GameResourcesError> resources =
        core::GameResources::create(resources_root);
    if (!resources.has_value())
    {
        std::println(stderr, "rock-hero: {}", resources.error().message);
        return false;
    }

    const core::ShaderBackend shader_backend = toShaderBackend(backend);
    const auto vertex_bytes = resources->shaderBytes(
        core::GameShaderProgram::SurfaceFlat, core::ShaderStage::Vertex, shader_backend);
    const auto fragment_bytes = resources->shaderBytes(
        core::GameShaderProgram::SurfaceFlat, core::ShaderStage::Fragment, shader_backend);
    if (!vertex_bytes.has_value() || !fragment_bytes.has_value())
    {
        std::println(
            stderr,
            "rock-hero: {}",
            vertex_bytes.has_value() ? fragment_bytes.error().message
                                     : vertex_bytes.error().message);
        return false;
    }

    const std::expected<void, RenderDeviceError> program =
        device.createSurfaceProgram(*vertex_bytes, *fragment_bytes);
    if (!program.has_value())
    {
        std::println(stderr, "rock-hero: {}", program.error().message);
        return false;
    }

    return true;
}

// Emits the Phase 3 timing channels: a per-frame trace record (dormant at the logger's default
// Info runtime level until the dev-diagnostics flag raises verbosity — plan 20 Phase 4) and a
// once-per-second pacing summary at Info so steady-state logs stay readable. mirror_age_ns
// logs -1 while the playback clock has never published (no engine lives in this process yet).
void logFrameInstrumentation(
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

    if (!loadSurfaceProgram(*device, defaultRenderBackend()))
    {
        return 1;
    }

    // The L2 frame loop: freshest input first, JUCE callbacks settled before the frame is built,
    // then the vsynced present paces the whole loop.
    core::FrameClock frame_clock;
    core::FramePacingStats pacing_stats;
    std::chrono::nanoseconds previous_frame_boundary{0};
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
        // composes the real IPlaybackClock — so the loop consumes an unpublished snapshot,
        // keeping the sanctioned song-time path live end to end. Song time never comes from
        // wall clock or frame counts (architecture.md "Timing and Latency").
        const std::chrono::nanoseconds frame_sample_time =
            std::chrono::steady_clock::now().time_since_epoch();
        const core::FrameClockSample frame_sample = frame_clock.sample(
            rock_hero::common::audio::PlaybackClockSnapshot{}, frame_sample_time);

        device->submitFrame();
        ++frames_submitted;

        // Post-frame stamp: the frame boundary that paces the loop. It is a pacing anchor, not
        // photon time — bgfx presents the PREVIOUS frame at the top of each submit, and DXGI
        // queues presents ahead; plan 13's video-offset calibration owns that quasi-constant
        // chain (magnitudes recorded in the plan-20 Phase 3 record).
        const std::chrono::nanoseconds frame_boundary_time =
            std::chrono::steady_clock::now().time_since_epoch();
        logFrameInstrumentation(
            *device, frame_sample, frame_boundary_time, previous_frame_boundary, pacing_stats);
        previous_frame_boundary = frame_boundary_time;

        if (options.frame_limit.has_value() && frames_submitted >= *options.frame_limit)
        {
            break;
        }
    }

    return 0;
}

} // namespace rock_hero::game::ui
