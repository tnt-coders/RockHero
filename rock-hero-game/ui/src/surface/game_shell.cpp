#include "surface/game_shell.h"

#include "surface/game_window.h"
#include "surface/juce_message_pump.h"
#include "surface/render_device.h"

#include <cstdint>
#include <cstdio>
#include <expected>
#include <juce_events/juce_events.h>

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
        std::fprintf(stderr, "rock-hero: %s\n", window.error().message.c_str());
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
        std::fprintf(stderr, "rock-hero: %s\n", device.error().message.c_str());
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

        device->submitClearedFrame();
        ++frames_submitted;

        if (options.frame_limit.has_value() && frames_submitted >= *options.frame_limit)
        {
            break;
        }
    }

    return 0;
}

} // namespace rock_hero::game::ui
