#include <bgfx/bgfx.h>
#include <bgfx/platform.h>
#include <chrono>
#include <cstdint>
#include <rock_hero/common/ui/render/render_device.h>
#include <utility>
#include <vector>

namespace rock_hero::common::ui
{

namespace
{

// Neutral dark clear color (0xRRGGBBAA) so the window reads as an intentional surface even when
// no scene renderer encoded content this frame; the highway's own views draw over it.
constexpr std::uint32_t g_clear_color = 0x1a1a22ff;

// The one view id the cleared frame touches. Shared with the highway renderer's background view
// (its g_background_view): when a scene draws, the renderer's per-frame setViewClear overrides
// this backstop color, and the touch here only keeps presentation alive when no renderer exists.
constexpr bgfx::ViewId g_default_view = 0;

// Platform table mapping project backends to bgfx renderer types. Deliberately pinned rather
// than bgfx auto-select (RendererType::Count): auto-select could land on a backend zero
// soak-minutes have exercised, and the gate's evidence is specifically for Direct3D 11.
[[nodiscard]] constexpr bgfx::RendererType::Enum toBgfxRendererType(
    const RenderBackend backend) noexcept
{
    switch (backend)
    {
        case RenderBackend::Direct3D11:
        {
            return bgfx::RendererType::Direct3D11;
        }
        case RenderBackend::Noop:
        {
            return bgfx::RendererType::Noop;
        }
    }
    return bgfx::RendererType::Noop;
}

} // namespace

// One-entry platform table today; future platforms extend the table, not the build graph.
RenderBackend defaultRenderBackend() noexcept
{
    return RenderBackend::Direct3D11;
}

// Fills the default diagnostic for each stable failure reason.
RenderDeviceError::RenderDeviceError(const RenderDeviceErrorCode error_code)
    : code{error_code}
{
    switch (error_code)
    {
        case RenderDeviceErrorCode::MissingNativeWindowHandle:
        {
            message = "Windowed render backend requested without a native window handle";
            break;
        }
        case RenderDeviceErrorCode::InitializationFailed:
        {
            message = "bgfx failed to initialize the requested render backend";
            break;
        }
    }
}

// Brings bgfx up in single-threaded mode. The renderFrame-before-init call is load-bearing: the
// Conan package compiles bgfx multithreaded, and calling renderFrame once on this thread before
// init suppresses the internal render thread, making this thread both API and render thread.
std::expected<RenderDevice, RenderDeviceError> RenderDevice::create(
    const RenderDeviceConfig& config)
{
    if (config.backend != RenderBackend::Noop && config.native_window_handle == nullptr)
    {
        return std::unexpected{RenderDeviceError{RenderDeviceErrorCode::MissingNativeWindowHandle}};
    }

    const std::uint32_t reset_flags = config.vsync ? BGFX_RESET_VSYNC : BGFX_RESET_NONE;

    bgfx::renderFrame();

    bgfx::Init init;
    init.type = toBgfxRendererType(config.backend);
    init.platformData.nwh = config.native_window_handle;
    init.resolution.width = config.width;
    init.resolution.height = config.height;
    init.resolution.reset = reset_flags;
    // Driven by the dev-diagnostics runtime flag (plan 20 Phase 4); bgfx degrades gracefully if
    // the D3D11 debug layers are absent. Profiling stays off until something consumes it.
    init.debug = config.debug;
    init.profile = false;

    if (!bgfx::init(init))
    {
        return std::unexpected{RenderDeviceError{RenderDeviceErrorCode::InitializationFailed}};
    }

    // View clear state persists per view, so configure it once here rather than every frame.
    bgfx::setViewClear(g_default_view, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, g_clear_color, 1.0F, 0);

    return RenderDevice{config.width, config.height, reset_flags};
}

// Adopts the initialized bgfx instance; only create() calls this.
RenderDevice::RenderDevice(
    const std::uint32_t width, const std::uint32_t height, const std::uint32_t reset_flags) noexcept
    : m_owns_device{true}
    , m_width{width}
    , m_height{height}
    , m_reset_flags{reset_flags}
{}

// Single-threaded teardown mirror: plain same-thread shutdown, no render-thread drain ceremony
// (that exists only for app-owned render threads in multithreaded mode). Every scene-owned bgfx
// handle (the highway renderer's) must already be gone — structural via declaration order in
// the shell.
RenderDevice::~RenderDevice()
{
    if (m_owns_device)
    {
        bgfx::shutdown();
    }
}

// Transfers bgfx ownership; the source keeps m_owns_device false so it tears nothing down.
RenderDevice::RenderDevice(RenderDevice&& other) noexcept
    : m_owns_device{std::exchange(other.m_owns_device, false)}
    , m_width{other.m_width}
    , m_height{other.m_height}
    , m_reset_flags{other.m_reset_flags}
{}

// Resizes the backbuffer only — the windowing system owns the window size. Passing the full
// stored flag set matters: reset flags are absolute, and dropping them would disable vsync.
void RenderDevice::resize(const std::uint32_t width, const std::uint32_t height)
{
    m_width = width;
    m_height = height;
    bgfx::reset(width, height, m_reset_flags);
}

std::uint32_t RenderDevice::width() const noexcept
{
    return m_width;
}

std::uint32_t RenderDevice::height() const noexcept
{
    return m_height;
}

// bgfx's built-in debug text is a global overlay drawn during frame(), independent of views —
// the cheapest possible overlay v1 until plan 20 Phase 4's diagnostics layer replaces it.
void RenderDevice::setDebugTextEnabled(const bool enabled)
{
    bgfx::setDebug(enabled ? BGFX_DEBUG_TEXT : BGFX_DEBUG_NONE);
}

void RenderDevice::clearDebugText()
{
    bgfx::dbgTextClear();
}

// Renders through dbgTextImage's char/attribute cell buffer: bgfx's printf-style debug-text
// entry points are C-vararg functions, which the project lint bans, and the text is already
// fully formatted by the caller anyway.
void RenderDevice::printDebugText(
    const std::uint16_t column, const std::uint16_t row, const std::string& text)
{
    if (text.empty())
    {
        return;
    }
    constexpr std::uint8_t g_white_on_transparent = 0x0f;
    std::vector<std::uint8_t> cells;
    cells.reserve(text.size() * 2);
    for (const char character : text)
    {
        cells.push_back(static_cast<std::uint8_t>(character));
        cells.push_back(g_white_on_transparent);
    }
    bgfx::dbgTextImage(
        column,
        row,
        static_cast<std::uint16_t>(text.size()),
        1,
        cells.data(),
        static_cast<std::uint16_t>(cells.size()));
}

// Converts bgfx's tick-based frame-period measurement to nanoseconds. getStats() is valid right
// after frame() on the API thread; cpuTimeFrame is stamped inside the frame() call that just
// returned, so it is fresh (unlike the render/GPU fields, which describe earlier frames).
std::chrono::nanoseconds RenderDevice::lastCpuFrameTime() const
{
    const bgfx::Stats* const stats = bgfx::getStats();
    if (stats->cpuTimerFreq == 0)
    {
        return std::chrono::nanoseconds{0};
    }
    return std::chrono::nanoseconds{
        stats->cpuTimeFrame * std::int64_t{1'000'000'000} /
        static_cast<std::int64_t>(stats->cpuTimerFreq)
    };
}

// Executes the backstop clear plus every view scene renderers encoded, then presents. touch()
// submits an empty draw so the clear runs even when nothing was encoded; frame() is where vsync
// blocks, pacing the loop.
void RenderDevice::submitFrame()
{
    bgfx::setViewRect(
        g_default_view,
        0,
        0,
        static_cast<std::uint16_t>(m_width),
        static_cast<std::uint16_t>(m_height));
    bgfx::touch(g_default_view);

    bgfx::frame();
}

} // namespace rock_hero::common::ui
