/*!
\file preview_surface.h
\brief bgfx render surface for the editor's 3D preview: child HWND, device, and frame ticks.
*/

#pragma once

#include <chrono>
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>
#include <optional>
#include <rock_hero/common/audio/clock/i_playback_clock.h>
#include <rock_hero/common/audio/clock/playback_clock_extrapolator.h>
#include <rock_hero/common/audio/transport/i_transport.h>
#include <rock_hero/common/core/highway/highway_view_state.h>
#include <rock_hero/common/ui/highway/highway_renderer.h>
#include <rock_hero/common/ui/render/render_device.h>

namespace rock_hero::editor::ui
{

/*!
\brief Hosts the shared highway renderer inside the preview window.

Owns a native child window embedded in the preview window's peer (the pattern the G20-RENDER
spike proved as criterion S2), the process bgfx device, and the shared highway renderer. Frames
tick on the message thread at vblank cadence — the editor's established display-refresh
mechanism — sampling song time from the playback clock while playing (block-quantized transport
reads shimmer on a moving field) and from the marker rule while paused: the armed caret is THE
paused position (the 2026-07-18 marker model), with the exact transport position as the passive
fallback, so a paused seek or caret move always lands.

Lifecycle: attach() brings the stack up against the current peer on first open and merely
resumes frame ticks on later opens; suspend() stops the ticks when the window hides (JUCE keeps
a hidden top-level's native peer — and with it our child window — alive, but vblank dispatch is
visibility-blind, so the ticks must stop explicitly). The bgfx device deliberately lives until
detach() at destruction: bgfx cannot be re-initialized after shutdown in the same process
(bgfx's renderFrame-before-init single-thread pin trips an internal assert on the second cycle),
so one init per editor process is a correctness requirement, not a cache.
*/
class PreviewSurface final : public juce::Component,
                             private juce::ComponentPeer::ScaleFactorListener
{
public:
    /*!
    \brief Creates the surface; no native or GPU state is touched until attach().
    \param transport Read-only transport for exact paused-cursor time.
    \param playback_clock Playback-time telemetry sampled while playing.
    */
    PreviewSurface(
        const common::audio::ITransport& transport,
        const common::audio::IPlaybackClock& playback_clock);

    /*! \brief Detaches (if attached) and destroys the surface. */
    ~PreviewSurface() override;

    PreviewSurface(const PreviewSurface&) = delete;
    PreviewSurface& operator=(const PreviewSurface&) = delete;
    PreviewSurface(PreviewSurface&&) = delete;
    PreviewSurface& operator=(PreviewSurface&&) = delete;

    /*! \brief Brings the render stack up on first open, resumes frame ticks on later opens. */
    void attach();

    /*! \brief Stops frame ticks while the window hides; the render stack stays alive. */
    void suspend();

    /*! \brief Stops frame ticks and destroys renderer, device, and child window, in that order. */
    void detach();

    /*!
    \brief Replaces the highway content the surface renders.
    \param state Shared scene-model snapshot from the controller; null clears the board.
    */
    void setHighwayState(std::shared_ptr<const common::core::HighwayViewState> state);

    /*!
    \brief Publishes the armed caret's timeline position for the paused-time marker rule.
    \param seconds Armed caret seconds, or nullopt while the marker is passive.
    */
    void setCaretSeconds(std::optional<double> seconds);

    /*! \brief Repositions the embedded child window over this component. */
    void resized() override;

    /*! \brief Repositions the embedded child window when the component moves. */
    void moved() override;

    /*! \brief Paints the fallback background (visible only when the render stack is down). */
    void paint(juce::Graphics& graphics) override;

private:
    // Monitor-scale changes keep the logical bounds constant, so resized()/moved() never fire;
    // this is the only reliable signal to recompute the physical child rect and backbuffer.
    void nativeScaleFactorChanged(double new_scale_factor) override;

    // Renders one frame: clock sample, highway draw, present.
    void renderFrame();

    // Moves the native child window to this component's physical-pixel bounds; returns the size.
    struct PixelSize
    {
        std::uint32_t width{0};
        std::uint32_t height{0};
    };
    PixelSize updateChildBounds();

    const common::audio::ITransport& m_transport;
    const common::audio::IPlaybackClock& m_playback_clock;

    // Native child window handle (HWND) bgfx renders into; null while detached. maybe_unused:
    // every consumer sits behind JUCE_WINDOWS, so non-Windows compile-hygiene builds never
    // reference it (the preview ships Windows-only for now, like the compiled shaders).
    [[maybe_unused]] void* m_child_window{nullptr};

    std::optional<common::ui::RenderDevice> m_device;
    std::optional<common::ui::HighwayRenderer> m_renderer;

    std::shared_ptr<const common::core::HighwayViewState> m_state;

    // The armed caret's timeline seconds while the marker is armed; the paused frame shows it
    // (the marker rule) and falls back to the transport position while passive.
    std::optional<double> m_caret_seconds;

    // Applied to the renderer on the next frame after a state swap.
    bool m_state_dirty{false};

    // One warning per attach when the embedded child window unexpectedly disappears.
    // maybe_unused for the same non-Windows reason as m_child_window.
    [[maybe_unused]] bool m_reported_lost_child{false};

    common::audio::PlaybackClockExtrapolator m_extrapolator;

    // Previous tick's timestamp for camera-smoothing frame deltas; zero on the first frame.
    std::chrono::nanoseconds m_previous_tick{0};

    std::unique_ptr<juce::VBlankAttachment> m_vblank;
};

} // namespace rock_hero::editor::ui
