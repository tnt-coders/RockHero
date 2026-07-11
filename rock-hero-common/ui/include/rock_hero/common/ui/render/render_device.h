/*!
\file render_device.h
\brief bgfx render device lifecycle: single-threaded init, per-frame submission, resize, teardown.
*/

#pragma once

#include <chrono>
#include <cstdint>
#include <expected>
#include <string>

namespace rock_hero::common::ui
{

/*!
\brief Render backends this project ships or tests against.

The gate pinned Direct3D 11 as the required proven Windows backend; Noop is the GPU-less,
window-less backend the headless test suite runs on (gate criterion S5). Adding a platform later
extends this enum and the platform table behind \ref defaultRenderBackend — an init-time choice,
never a build-graph change.
*/
enum class RenderBackend : std::uint8_t
{
    /*! \brief Direct3D 11 — the spike-proven production backend on Windows. */
    Direct3D11,

    /*! \brief bgfx's no-op backend for headless tests: no GPU, no window, no platform data. */
    Noop,
};

/*!
\brief Returns the production render backend for the platform this build targets.

\return The backend the game window renders with (one-entry table today: Windows → Direct3D 11).
*/
[[nodiscard]] RenderBackend defaultRenderBackend() noexcept;

/*!
\brief Backbuffer multisample anti-aliasing levels.

bgfx clamps a requested level to the nearest one the device supports, so these are requests, not
guarantees — though every Direct3D 11 device guarantees 4x for the backbuffer format.
*/
enum class RenderMsaa : std::uint8_t
{
    /*! \brief No multisampling; edges alias. */
    None,

    /*! \brief 2x multisampling. */
    X2,

    /*! \brief 4x multisampling — the default request. */
    X4,

    /*! \brief 8x multisampling. */
    X8,

    /*! \brief 16x multisampling. */
    X16,
};

/*! \brief Stable reasons a render device can fail to come up. */
enum class RenderDeviceErrorCode : std::uint8_t
{
    /*! \brief A windowed backend was requested without a native window handle to render into. */
    MissingNativeWindowHandle,

    /*! \brief bgfx initialization failed for the requested backend. */
    InitializationFailed,
};

/*! \brief Typed boundary error for render-device startup failures. */
struct [[nodiscard]] RenderDeviceError
{
    /*! \brief Stable failure reason for program branching. */
    RenderDeviceErrorCode code{};

    /*! \brief Human-readable diagnostic for UI display or logs. */
    std::string message;

    /*!
    \brief Creates an error with the default message for the code.
    \param error_code Stable failure reason.
    */
    explicit RenderDeviceError(RenderDeviceErrorCode error_code);
};

/*! \brief Everything \ref RenderDevice::create needs to bring bgfx up. */
struct RenderDeviceConfig
{
    /*! \brief Backend to initialize. */
    RenderBackend backend = RenderBackend::Direct3D11;

    /*! \brief Native window handle to render into; null only for the Noop backend. */
    void* native_window_handle = nullptr;

    /*! \brief Initial backbuffer width in pixels. */
    std::uint32_t width = 0;

    /*! \brief Initial backbuffer height in pixels. */
    std::uint32_t height = 0;

    /*! \brief True enables vsync — the gate's default frame-pacing policy (20-Q6: A). */
    bool vsync = true;

    /*!
    \brief Backbuffer multisample level.

    The highway scene is thin-line-heavy (fret lines, strings, lane ribbons), so its edges alias
    visibly without multisampling; 4x is the request every consumer inherits by default.
    */
    RenderMsaa msaa = RenderMsaa::X4;

    /*!
    \brief True enables bgfx's debug checks and the D3D11 SDK debug layers when present.

    Wired to the dev-diagnostics runtime flag (plan 20 Phase 4, 20-Q5: A); bgfx degrades
    gracefully when the debug layers are not installed.
    */
    bool debug = false;
};

/*!
\brief Owns the process-wide bgfx instance in single-threaded mode.

bgfx is a process singleton, so this type is move-only and at most one live instance may exist.
Every member — creation, frames, resize, destruction — must run on the same thread: creation pins
bgfx's single-threaded mode (render-frame-before-init), making the calling thread both API and
render thread. Under loop model L2 that is the main thread. bgfx's native render-thread split is
the recorded escalation if a heavy scene ever strains the message thread; until then this is the
settled mode.
*/
class RenderDevice
{
public:
    /*!
    \brief Initializes bgfx single-threaded for the configured backend and window.

    \param config Backend, target window, initial size, pacing, and anti-aliasing policy.
    \return The initialized device, or a typed error when bgfx refuses to come up.
    */
    [[nodiscard]] static std::expected<RenderDevice, RenderDeviceError> create(
        const RenderDeviceConfig& config);

    /*! \brief Shuts bgfx down when this object still owns the instance. */
    ~RenderDevice();

    /*!
    \brief Transfers bgfx ownership; the source is left inert and tears nothing down.
    \param other Device losing ownership.
    */
    RenderDevice(RenderDevice&& other) noexcept;

    RenderDevice(const RenderDevice&) = delete;
    RenderDevice& operator=(const RenderDevice&) = delete;
    RenderDevice& operator=(RenderDevice&&) = delete;

    /*!
    \brief Resizes the backbuffer to a new pixel size.

    Call with sizes from the window's pixel-size query (never window coordinates); the window
    itself is resized by the windowing system, this only follows with the backbuffer.

    \param width New backbuffer width in pixels.
    \param height New backbuffer height in pixels.
    */
    void resize(std::uint32_t width, std::uint32_t height);

    /*!
    \brief Submits one frame — the backstop clear plus everything scene renderers encoded — and
    presents it.

    Scene content (the highway renderer) encodes its views before this call; this executes and
    presents them. With vsync on, the present blocks until the display's next refresh — this
    call is the frame pacer of the main loop. Present semantics (Phase 3 checkpoint): bgfx flips
    BEFORE it renders, so each call presents the PREVIOUS frame's content and then executes this
    frame's commands; a timestamp taken after this returns is a pacing anchor, never a photon
    time.
    */
    void submitFrame();

    /*!
    \brief Current backbuffer width in pixels (scene renderers size their view rects from this).
    \return Backbuffer width.
    */
    [[nodiscard]] std::uint32_t width() const noexcept;

    /*!
    \brief Current backbuffer height in pixels.
    \return Backbuffer height.
    */
    [[nodiscard]] std::uint32_t height() const noexcept;

    /*!
    \brief Enables or disables bgfx's built-in debug-text overlay (the Phase 3 overlay v1).
    \param enabled True to draw queued debug text over every frame.
    */
    void setDebugTextEnabled(bool enabled);

    /*! \brief Clears the queued debug text; call once per frame before printing. */
    void clearDebugText();

    /*!
    \brief Queues one line of debug text at a character cell position.
    \param column Character column from the left edge.
    \param row Character row from the top edge.
    \param text Line content.
    */
    void printDebugText(std::uint16_t column, std::uint16_t row, const std::string& text);

    /*!
    \brief bgfx's own measurement of the last frame period, as a cross-check channel.

    This is bgfx's cpuTimeFrame — the time between the two most recent submitFrame() swaps,
    stamped inside bgfx. The loop logs it beside its own steady-clock frame delta; the two should
    track within noise, and divergence is itself a diagnostic.

    \return Last frame period per bgfx's high-precision counter.
    */
    [[nodiscard]] std::chrono::nanoseconds lastCpuFrameTime() const;

private:
    // Adopts an initialized bgfx instance; only create() calls this.
    RenderDevice(std::uint32_t width, std::uint32_t height, std::uint32_t reset_flags) noexcept;

    // True while this object owns the process bgfx instance and must shut it down.
    bool m_owns_device = false;

    // Current backbuffer size, kept in sync by resize() for per-frame view rects.
    std::uint32_t m_width = 0;
    std::uint32_t m_height = 0;

    // Full reset-flag set. bgfx reset flags are absolute state, not a delta: every bgfx::reset
    // must carry all current flags or previously-set ones (vsync) silently turn off.
    std::uint32_t m_reset_flags = 0;
};

} // namespace rock_hero::common::ui
