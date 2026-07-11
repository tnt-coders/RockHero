#include "preview/preview_surface.h"

#include "preview/preview_resources.h"

#include <expected>
#include <rock_hero/common/audio/clock/playback_clock_snapshot.h>
#include <rock_hero/common/core/shared/logger.h>
#include <rock_hero/common/core/timeline/timeline.h>
#include <utility>

#if JUCE_WINDOWS
#define WIN32_LEAN_AND_MEAN
// Keep windows.h's min/max macros from shadowing std::min/std::max below.
#define NOMINMAX
#include <windows.h>
#endif

namespace rock_hero::editor::ui
{

namespace
{

#if JUCE_WINDOWS

// Registers (once) a paint-inert window class for the embedded render child: no background
// brush, default proc — the swapchain owns every pixel, so Windows must never erase it.
[[nodiscard]] const wchar_t* previewChildWindowClass()
{
    static const wchar_t* g_class_name = [] {
        WNDCLASSEXW window_class{};
        window_class.cbSize = sizeof(WNDCLASSEXW);
        window_class.lpfnWndProc = DefWindowProcW;
        window_class.hInstance = GetModuleHandleW(nullptr);
        window_class.lpszClassName = L"RockHeroPreviewSurface";
        RegisterClassExW(&window_class);
        return L"RockHeroPreviewSurface";
    }();
    return g_class_name;
}

#endif

} // namespace

PreviewSurface::PreviewSurface(
    const common::audio::ITransport& transport, const common::audio::IPlaybackClock& playback_clock)
    : m_transport{transport}
    , m_playback_clock{playback_clock}
{
    setOpaque(true);
}

PreviewSurface::~PreviewSurface()
{
    detach();
}

// Brings up the native child, the bgfx device, and the shared renderer against the current
// peer. Attach order matters: the child window must exist before bgfx initializes against it,
// and the renderer needs a live device for its GPU resources.
void PreviewSurface::attach()
{
#if JUCE_WINDOWS
    if (m_device.has_value() || getPeer() == nullptr)
    {
        return;
    }

    auto* parent = static_cast<HWND>(getPeer()->getNativeHandle());
    m_child_window = CreateWindowExW(
        0,
        previewChildWindowClass(),
        nullptr,
        WS_CHILD | WS_VISIBLE,
        0,
        0,
        16,
        16,
        parent,
        nullptr,
        GetModuleHandleW(nullptr),
        nullptr);
    if (m_child_window == nullptr)
    {
        RH_LOG_ERROR("editor.preview", "preview child window creation failed");
        return;
    }
    const PixelSize size = updateChildBounds();

    std::expected<common::ui::RenderDevice, common::ui::RenderDeviceError> device =
        common::ui::RenderDevice::create(
            common::ui::RenderDeviceConfig{
                .backend = common::ui::defaultRenderBackend(),
                .native_window_handle = m_child_window,
                .width = size.width,
                .height = size.height,
                .vsync = true,
                .debug = false,
            });
    if (!device.has_value())
    {
        RH_LOG_ERROR("editor.preview", "{}", device.error().message);
        DestroyWindow(static_cast<HWND>(m_child_window));
        m_child_window = nullptr;
        return;
    }
    m_device.emplace(std::move(*device));

    std::optional<common::ui::HighwayShaderSet> shaders = loadPreviewHighwayShaders();
    if (!shaders.has_value())
    {
        RH_LOG_ERROR("editor.preview", "preview shaders unavailable; preview disabled");
        detach();
        return;
    }
    std::expected<common::ui::HighwayRenderer, common::ui::HighwayRendererError> renderer =
        common::ui::HighwayRenderer::create(*shaders, loadPreviewHighwayTextures());
    if (!renderer.has_value())
    {
        RH_LOG_ERROR("editor.preview", "{}", renderer.error().message);
        detach();
        return;
    }
    m_renderer.emplace(std::move(*renderer));
    m_state_dirty = m_state != nullptr;

    m_extrapolator.reset();
    m_previous_tick = std::chrono::nanoseconds{0};
    m_vblank = std::make_unique<juce::VBlankAttachment>(this, [this] { renderFrame(); });
#endif
}

// Teardown mirrors attach in reverse: renderer (GPU handles) before the device (bgfx shutdown),
// device before the child window it presents into.
void PreviewSurface::detach()
{
    m_vblank.reset();
    m_renderer.reset();
    m_device.reset();
#if JUCE_WINDOWS
    if (m_child_window != nullptr)
    {
        DestroyWindow(static_cast<HWND>(m_child_window));
        m_child_window = nullptr;
    }
#endif
}

void PreviewSurface::setHighwayState(std::shared_ptr<const common::core::HighwayViewState> state)
{
    m_state = std::move(state);
    m_state_dirty = true;
}

void PreviewSurface::resized()
{
    updateChildBounds();
}

void PreviewSurface::moved()
{
    updateChildBounds();
}

// Positions the child window over this component in the peer's physical-pixel space and keeps
// the backbuffer in step.
PreviewSurface::PixelSize PreviewSurface::updateChildBounds()
{
#if JUCE_WINDOWS
    if (m_child_window == nullptr || getPeer() == nullptr)
    {
        return {};
    }

    // Component bounds are logical; the peer's child coordinate space is physical pixels.
    const double scale = getPeer()->getPlatformScaleFactor();
    const juce::Rectangle<int> local = getTopLevelComponent()->getLocalArea(this, getLocalBounds());
    const auto x = static_cast<int>(local.getX() * scale);
    const auto y = static_cast<int>(local.getY() * scale);
    const auto width = static_cast<int>(local.getWidth() * scale);
    const auto height = static_cast<int>(local.getHeight() * scale);
    MoveWindow(
        static_cast<HWND>(m_child_window), x, y, std::max(width, 1), std::max(height, 1), TRUE);

    const PixelSize size{
        .width = static_cast<std::uint32_t>(std::max(width, 1)),
        .height = static_cast<std::uint32_t>(std::max(height, 1)),
    };
    if (m_device.has_value() &&
        (m_device->width() != size.width || m_device->height() != size.height))
    {
        m_device->resize(size.width, size.height);
    }
    return size;
#else
    return {};
#endif
}

// One message-thread frame at vblank cadence: coherent time sample, highway draw, present. With
// vsync on and vblank-aligned ticks the present returns without long blocking (the S2 pattern:
// surrounding JUCE paints were never starved).
void PreviewSurface::renderFrame()
{
    if (!m_device.has_value() || !m_renderer.has_value())
    {
        return;
    }

    if (m_state_dirty)
    {
        m_state_dirty = false;
        m_renderer->setViewState(m_state != nullptr ? *m_state : common::core::HighwayViewState{});
    }

    const std::chrono::nanoseconds now = std::chrono::steady_clock::now().time_since_epoch();
    const double dt_seconds = m_previous_tick == std::chrono::nanoseconds{0}
                                  ? 0.0
                                  : static_cast<double>((now - m_previous_tick).count()) / 1.0e9;
    m_previous_tick = now;

    // Song time: the clock port plus extrapolation while playing (plan 12's policy — raw
    // transport reads shimmer on a moving field); the exact transport position while paused so
    // paused seeks always land even if the clock publisher is idle.
    const common::audio::PlaybackClockSnapshot snapshot = m_playback_clock.snapshot();
    double song_seconds = 0.0;
    if (snapshot.playing)
    {
        song_seconds = m_extrapolator.advance(snapshot, now).seconds;
    }
    else
    {
        m_extrapolator.reset();
        song_seconds = m_transport.position().seconds;
    }

    m_renderer->draw(song_seconds, dt_seconds, m_device->width(), m_device->height());
    m_device->submitFrame();
}

} // namespace rock_hero::editor::ui
