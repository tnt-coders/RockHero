#include "surface/game_shell.h"

#include "surface/game_window.h"
#include "surface/juce_message_pump.h"
#include "surface/render_device.h"

#include <SDL3/SDL.h>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <juce_events/juce_events.h>
#include <optional>
#include <print>
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

        device->submitFrame();
        ++frames_submitted;

        if (options.frame_limit.has_value() && frames_submitted >= *options.frame_limit)
        {
            break;
        }
    }

    return 0;
}

} // namespace rock_hero::game::ui
