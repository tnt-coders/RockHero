/*!
\file preview_surface.h
\brief bgfx render surface for the editor's 3D preview: child HWND, device, and frame ticks.
*/

#pragma once

#include "preview/preview_time_model.h"

#include <chrono>
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>
#include <optional>
#include <rock_hero/common/audio/clock/i_playback_clock.h>
#include <rock_hero/common/audio/transport/i_transport.h>
#include <rock_hero/common/core/highway/highway_view_state.h>
#include <rock_hero/common/ui/highway/highway_renderer.h>
#include <rock_hero/common/ui/highway_theme/loaded_highway_theme.h>
#include <rock_hero/common/ui/render/render_device.h>
#include <rock_hero/common/ui/string_colors/string_color_palette.h>

namespace rock_hero::editor::ui
{

/*!
\brief Hosts the shared highway renderer inside the preview window.

Owns a native child window embedded in the preview window's peer (the spike-proven hosting
docs/design/architecture.md "Game View" describes), the process bgfx device, and the shared
highway renderer. Frames tick on the message thread at vblank cadence — the editor's established
display-refresh mechanism — sampling song time from the playback clock while playing
(block-quantized transport reads shimmer on a moving field) and from the timeline's paused mark
while paused (TrackViewport::pausedMarkSeconds: the armed caret, chart or lane, else a held
tempo-chip drag's cursor), with the exact transport position as the fallback, so a paused seek or
caret move always lands. Paused time glides toward its
target with a short exponential settle, so navigation reads as motion down the highway, not a
cut.

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
    \param theme The theme the highway draws with until \ref setLook; must outlive the surface.
    \param palette The string colors the highway draws with until \ref setLook.
    */
    PreviewSurface(
        const common::audio::ITransport& transport,
        const common::audio::IPlaybackClock& playback_clock,
        const common::ui::LoadedHighwayTheme& theme, const common::ui::StringColorPalette& palette);

    /*! \brief Detaches (if attached) and destroys the surface. */
    ~PreviewSurface() override;

    /*! \brief Copying is disabled because the surface owns a native window and a GPU device. */
    PreviewSurface(const PreviewSurface&) = delete;

    /*! \brief Copy assignment is disabled because the surface owns native and GPU state. */
    PreviewSurface& operator=(const PreviewSurface&) = delete;

    /*! \brief Moving is disabled because the native child window is bound to this component. */
    PreviewSurface(PreviewSurface&&) = delete;

    /*! \brief Move assignment is disabled because the native child window is bound to this. */
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
    \brief Re-frames the board from rest on the next frame, after any pending state swap.

    For a different project or arrangement; an edit of the one on screen only replaces the state,
    so the camera keeps gliding. A reopen re-frames on its own.
    */
    void resetCamera();

    /*!
    \brief Sets how the board maps onto the screen: the player's reading choices and the lane
    minimum the 2D tab pads to.
    \param options Display mapping, handed to the renderer now and at every later bring-up.
    */
    void setDisplayOptions(common::core::HighwayDisplayOptions options);

    /*!
    \brief Switches the highway to another look.

    A look with the same art only recolors the live renderer. Other art recreates the renderer
    through the bring-up path, which keeps the device: a failure logs and leaves the black
    fallback until the next open retries.

    \param theme The theme to draw with; must outlive the surface.
    \param palette The string colors to draw with.
    */
    void setLook(
        const common::ui::LoadedHighwayTheme& theme, const common::ui::StringColorPalette& palette);

    /*!
    \brief Publishes the timeline's paused mark (TrackViewport::pausedMarkSeconds) for paused time.
    \param seconds The paused mark's seconds, or nullopt while it is the transport's rest.
    */
    void setPausedMarkSeconds(std::optional<double> seconds);

    /*! \brief Repositions the embedded child window over this component. */
    void resized() override;

    /*! \brief Repositions the embedded child window when the component moves. */
    void moved() override;

    /*!
    \brief Paints the fallback background (visible only when the render stack is down).
    \param graphics Graphics context used for drawing.
    */
    void paint(juce::Graphics& graphics) override;

private:
    // Monitor-scale changes keep the logical bounds constant, so resized()/moved() never fire;
    // this is the only reliable signal to recompute the physical child rect and backbuffer.
    void nativeScaleFactorChanged(double new_scale_factor) override;

    // Brings up (or retries) the shaders and renderer against the live device without ever
    // re-initializing bgfx; returns whether the renderer is now live. Failure leaves the device
    // untouched so a later open can retry.
    bool bringUpRenderer();

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

    // The look each renderer bring-up creates the renderer with: the theme's colors and art, never
    // null, and the string colors resolved for it, which a pinned palette may take from another
    // theme.
    const common::ui::LoadedHighwayTheme* m_theme;
    common::ui::StringColorPalette m_palette;

    // Native child window handle (HWND) bgfx renders into; null while detached. maybe_unused:
    // every consumer sits behind JUCE_WINDOWS, so non-Windows compile-hygiene builds never
    // reference it (the preview ships Windows-only for now, like the compiled shaders).
    [[maybe_unused]] void* m_child_window{nullptr};

    std::optional<common::ui::RenderDevice> m_device;
    std::optional<common::ui::HighwayRenderer> m_renderer;

    std::shared_ptr<const common::core::HighwayViewState> m_state;

    // How the preview maps the board: the player's view preferences, padded to the tab's
    // displayed-string minimum. Handed to the renderer when it comes up and whenever the editor
    // restates it; the renderer ignores an unchanged set.
    common::core::HighwayDisplayOptions m_display_options{};

    // The timeline's paused mark; the paused frame shows it and falls back to the transport
    // position while it is empty.
    std::optional<double> m_paused_mark_seconds;

    // Applied to the renderer on the next frame after a state swap.
    bool m_state_dirty{false};

    // Applied to the renderer on the next frame, after the state swap (resetCamera, a reopen).
    bool m_camera_reset_pending{false};

    // One warning per attach when the embedded child window unexpectedly disappears.
    // maybe_unused for the same non-Windows reason as m_child_window.
    [[maybe_unused]] bool m_reported_lost_child{false};

    // Playing/paused displayed-time policy (extrapolation + paused glide); see PreviewTimeModel.
    PreviewTimeModel m_time_model;

    // Previous tick's timestamp for camera-smoothing frame deltas; zero on the first frame.
    std::chrono::nanoseconds m_previous_tick{0};

    std::unique_ptr<juce::VBlankAttachment> m_vblank;
};

} // namespace rock_hero::editor::ui
