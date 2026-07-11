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
reads shimmer on a moving field) and from the transport exactly while paused, so a paused seek
always lands.

Lifecycle: attach() brings the whole stack up against the current peer and detach() tears it
down renderer-first; the preview window drives both around show/hide because hiding a JUCE
top-level destroys its native peer (and with it the embedded child window bgfx renders into).
*/
class PreviewSurface final : public juce::Component
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

    /*! \brief Creates the child window, the bgfx device, and the renderer; starts frame ticks. */
    void attach();

    /*! \brief Stops frame ticks and destroys renderer, device, and child window, in that order. */
    void detach();

    /*!
    \brief Replaces the highway content the surface renders.
    \param state Shared scene-model snapshot from the controller; null clears the board.
    */
    void setHighwayState(std::shared_ptr<const common::core::HighwayViewState> state);

    /*! \brief Repositions the embedded child window over this component. */
    void resized() override;

    /*! \brief Repositions the embedded child window when the component moves. */
    void moved() override;

private:
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

    // Native child window handle (HWND) bgfx renders into; null while detached.
    void* m_child_window{nullptr};

    std::optional<common::ui::RenderDevice> m_device;
    std::optional<common::ui::HighwayRenderer> m_renderer;

    std::shared_ptr<const common::core::HighwayViewState> m_state;

    // Applied to the renderer on the next frame after a state swap.
    bool m_state_dirty{false};

    common::audio::PlaybackClockExtrapolator m_extrapolator;

    // Previous tick's timestamp for camera-smoothing frame deltas; zero on the first frame.
    std::chrono::nanoseconds m_previous_tick{0};

    std::unique_ptr<juce::VBlankAttachment> m_vblank;
};

} // namespace rock_hero::editor::ui
