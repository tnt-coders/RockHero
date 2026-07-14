/*!
\file sdl3_application.h
\brief SDL3 application framework base: the frame loop, JUCE-message drain, and vsync pacing.
*/

#pragma once

#include <chrono>
#include <cstdint>
#include <optional>
#include <rock_hero/game/core/frame_clock/frame_clock.h>

namespace rock_hero::game::ui
{

/*!
\brief Framework base owning the game's frame loop, JUCE-message drain, and pacing instrumentation.

SDL3Application is the game's counterpart of the editor's vendor `juce::JUCEApplication`: the thin
framework host that owns the run loop and lifecycle while a concrete subclass supplies the content.
Under loop model L2 (SDL owns the frame loop on the thread JUCE binds as its message thread), this
base owns exactly the framework plumbing that is not app-specific — the `while` loop cadence, the
bounded per-frame drain of JUCE's pending message queue, the frame-limit smoke hook, and the
vsync-paced frame instrumentation (the pacing accumulator, the previous-frame boundary stamp, and
the once-per-second pacing summary the overlay displays).

It deliberately owns no window and no render device: the subclass (the composition root) owns those,
mirroring how the editor's `RockHeroEditor` owns its `MainWindow` rather than `juce::JUCEApplication`
owning it. \ref run is a template method that calls the virtual \ref onInit / \ref onInput /
\ref onFrame / \ref onShutdown hooks the subclass overrides; the drain and the pacing sit between
those hooks, exactly where the concrete loop needs them, so the subclass never touches loop cadence.
*/
class SDL3Application
{
public:
    /*!
    \brief Creates the application base with the loop's frame-limit smoke hook.
    \param frame_limit When set, \ref run exits cleanly after this many submitted frames; empty runs
    until the window requests quit. Used by automated verification and smoke checks.
    */
    explicit SDL3Application(std::optional<std::uint64_t> frame_limit);

    SDL3Application(const SDL3Application&) = delete;
    SDL3Application& operator=(const SDL3Application&) = delete;
    SDL3Application(SDL3Application&&) = delete;
    SDL3Application& operator=(SDL3Application&&) = delete;
    virtual ~SDL3Application() = default;

    /*!
    \brief Runs the application: init, then the frame loop, then shutdown.

    Calls \ref onInit once; on an init failure (a returned exit code) it returns that code
    immediately without entering the loop or calling \ref onShutdown, leaving the subclass's own
    partially-built locals to unwind. Otherwise it drives the frame loop — freshest input first via
    \ref onInput, then the bounded JUCE-message drain, then \ref onFrame to build and submit the
    frame, then the vsync-paced instrumentation — until the window requests quit or the frame limit
    is reached, and finally calls \ref onShutdown before returning zero.

    Must be called on the process main thread (the thread JUCE binds as its message thread).

    \return Process exit code: zero on a clean quit, or the nonzero code \ref onInit reported.
    */
    [[nodiscard]] int run();

protected:
    /*! \brief Loop directive returned by \ref onInput: keep running, or quit the frame loop. */
    enum class FrameControl : std::uint8_t
    {
        /*! \brief Continue into this frame's drain, build, and present. */
        Continue,

        /*! \brief The window requested quit; leave the loop before drawing this frame. */
        Quit,
    };

    /*! \brief One frame's timing readout the base needs for pacing instrumentation. */
    struct FrameTiming
    {
        /*! \brief This frame's clock sample (song time plus instrumentation), from the content. */
        core::FrameClockSample sample{};

        /*! \brief The render device's CPU time for the submitted frame, for the trace channel. */
        std::chrono::nanoseconds cpu_frame_time{0};
    };

    /*!
    \brief Builds the application: window, render device, resources, and content.

    Called once at the start of \ref run. Fallible setup reports through the return value.

    \return Empty on success; a nonzero process exit code when startup failed.
    */
    [[nodiscard]] virtual std::optional<int> onInit() = 0;

    /*!
    \brief Consumes this frame's freshest input, before the JUCE-message drain.

    Polls the window and applies resize plus content input for this frame. Runs first each frame so
    input and size changes are as fresh as possible.

    \return \ref FrameControl::Quit when the window requested quit, else \ref FrameControl::Continue.
    */
    [[nodiscard]] virtual FrameControl onInput() = 0;

    /*!
    \brief Builds and submits this frame; returns its timing for the base's pacing.

    Called after the per-frame JUCE-message drain. Advances the content for this frame's clock time,
    draws it (using \p pacing_summary for the overlay's timing readout), and presents the frame.

    \param pacing_summary Latest closed pacing window for the overlay, or empty before the first
    window closes.
    \return This frame's clock sample and CPU frame time, for the base's frame instrumentation.
    */
    [[nodiscard]] virtual FrameTiming onFrame(
        const std::optional<core::FramePacingSummary>& pacing_summary) = 0;

    /*!
    \brief Tears the application down in teardown order, at the end of \ref run.

    Releases the render/content stack (scene handles before the bgfx device, the device before the
    window) so those die when \ref run returns, before the caller tears down the injected session
    and engine.
    */
    virtual void onShutdown() = 0;

private:
    // Pacing/instrumentation state owned by the loop, spanning frames.
    core::FramePacingStats m_pacing_stats;
    std::optional<core::FramePacingSummary> m_last_pacing_summary;
    std::chrono::nanoseconds m_previous_frame_boundary{0};
    std::uint64_t m_frames_submitted{0};

    // The smoke-run frame limit; empty runs until the window requests quit.
    std::optional<std::uint64_t> m_frame_limit;
};

} // namespace rock_hero::game::ui
