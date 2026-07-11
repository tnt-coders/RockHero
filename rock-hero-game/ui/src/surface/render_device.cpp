#include "surface/render_device.h"

#include "surface/bgfx_handle.h"

#include <array>
#include <bgfx/bgfx.h>
#include <bgfx/platform.h>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <utility>

namespace rock_hero::game::ui
{

namespace
{

// The header stores the program handle as a raw index with UINT16_MAX standing in for bgfx's
// invalid-handle sentinel; pin that equivalence so an upstream change breaks the build, not
// the teardown logic.
static_assert(bgfx::kInvalidHandle == UINT16_MAX);

// Neutral dark clear color (0xRRGGBBAA) so the Phase 1 window reads as an intentional surface
// rather than an uninitialized buffer; real scene content replaces it in later phases.
constexpr std::uint32_t g_clear_color = 0x1a1a22ff;

// The one view id the cleared frame touches; view ordering for real passes is decided at the
// plan 25 Phase 3 expert checkpoint.
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
        case RenderDeviceErrorCode::ShaderProgramCreationFailed:
        {
            message = "bgfx rejected a compiled shader binary or failed to link the program";
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
    // Explicitly off by default; the dev-diagnostics layer (plan 20 Phase 4) wires these to the
    // runtime dev flag. bgfx degrades gracefully if the D3D11 debug layers are absent.
    init.debug = false;
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
// (that exists only for app-owned render threads in multithreaded mode). The program (which holds
// the stage shaders' remaining references) must go before the instance does.
RenderDevice::~RenderDevice()
{
    if (m_owns_device)
    {
        if (m_program_handle != bgfx::kInvalidHandle)
        {
            bgfx::destroy(bgfx::ProgramHandle{m_program_handle});
        }
        bgfx::shutdown();
    }
}

// Transfers bgfx ownership; the source keeps m_owns_device false so it tears nothing down. The
// program handle moves too: the source must not keep a live-looking handle a later submitFrame
// on the moved-from object could submit with.
RenderDevice::RenderDevice(RenderDevice&& other) noexcept
    : m_owns_device{std::exchange(other.m_owns_device, false)}
    , m_program_handle{std::exchange(other.m_program_handle, bgfx::kInvalidHandle)}
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

// Links the surface program from compiled binaries. bgfx::copy snapshots the spans, so the
// caller's buffers may die immediately after this returns. The stage handles are RAII-owned and
// destroyed unconditionally at scope exit — correct on every path because destroyShaders stays
// false (a project rule: bgfx consumes the stage handles on some createProgram failure paths but
// not others, so the consuming flag makes correct cleanup impossible); on success the program
// holds its own shader references and keeps the stages alive until it is destroyed.
std::expected<void, RenderDeviceError> RenderDevice::createSurfaceProgram(
    const std::span<const std::byte> vertex_shader,
    const std::span<const std::byte> fragment_shader)
{
    const UniqueBgfxHandle<bgfx::ShaderHandle> vertex_handle{bgfx::createShader(
        bgfx::copy(vertex_shader.data(), static_cast<std::uint32_t>(vertex_shader.size())))};
    const UniqueBgfxHandle<bgfx::ShaderHandle> fragment_handle{bgfx::createShader(
        bgfx::copy(fragment_shader.data(), static_cast<std::uint32_t>(fragment_shader.size())))};
    if (!vertex_handle.isValid() || !fragment_handle.isValid())
    {
        return std::unexpected{
            RenderDeviceError{RenderDeviceErrorCode::ShaderProgramCreationFailed}
        };
    }

    const bgfx::ProgramHandle program =
        bgfx::createProgram(vertex_handle.get(), fragment_handle.get(), false);
    if (!bgfx::isValid(program))
    {
        return std::unexpected{
            RenderDeviceError{RenderDeviceErrorCode::ShaderProgramCreationFailed}
        };
    }

    m_program_handle = program.idx;
    return {};
}

namespace
{

// One vertex of the surface test triangle: clip-space position plus packed ABGR color.
struct SurfaceVertex
{
    float x;
    float y;
    float z;
    std::uint32_t abgr;
};

// Vertex layout matching SurfaceVertex and the surface_flat shader's inputs. Built lazily
// because bgfx::VertexLayout::begin needs the renderer type, which exists only after init.
[[nodiscard]] const bgfx::VertexLayout& surfaceVertexLayout()
{
    static const bgfx::VertexLayout g_layout = [] {
        bgfx::VertexLayout layout;
        layout.begin()
            .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
            .add(bgfx::Attrib::Color0, 4, bgfx::AttribType::Uint8, true)
            .end();
        return layout;
    }();
    return g_layout;
}

} // namespace

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

// Clears, draws the surface program's test triangle when one is loaded, and presents. touch()
// submits an empty draw so the view's clear executes even with no geometry; frame() is where
// vsync blocks, pacing the loop.
void RenderDevice::submitFrame()
{
    bgfx::setViewRect(
        g_default_view,
        0,
        0,
        static_cast<std::uint16_t>(m_width),
        static_cast<std::uint16_t>(m_height));
    bgfx::touch(g_default_view);

    if (m_program_handle != bgfx::kInvalidHandle)
    {
        // Transient geometry: three clip-space vertices re-uploaded per frame. Colors are packed
        // ABGR (bgfx's normalized-Uint8 color convention). The vertices are usable as clip-space
        // positions only because no view or model transform is ever set — bgfx defaults both to
        // identity; a future pass that sets view transforms must not reuse view 0 for this draw.
        constexpr std::array<SurfaceVertex, 3> vertices{
            SurfaceVertex{.x = 0.0F, .y = 0.5F, .z = 0.0F, .abgr = 0xff408080},
            SurfaceVertex{.x = 0.5F, .y = -0.5F, .z = 0.0F, .abgr = 0xff804080},
            SurfaceVertex{.x = -0.5F, .y = -0.5F, .z = 0.0F, .abgr = 0xff808040},
        };

        const bgfx::VertexLayout& layout = surfaceVertexLayout();
        const auto vertex_count = static_cast<std::uint32_t>(vertices.size());
        if (bgfx::getAvailTransientVertexBuffer(vertex_count, layout) >= vertex_count)
        {
            bgfx::TransientVertexBuffer transient_buffer{};
            bgfx::allocTransientVertexBuffer(&transient_buffer, vertex_count, layout);
            std::memcpy(transient_buffer.data, vertices.data(), sizeof(vertices));
            bgfx::setVertexBuffer(0, &transient_buffer);
            bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A);
            bgfx::submit(g_default_view, bgfx::ProgramHandle{m_program_handle});
        }
    }

    bgfx::frame();
}

} // namespace rock_hero::game::ui
