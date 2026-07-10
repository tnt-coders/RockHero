#include "surface/render_device.h"

#include <catch2/catch_test_macros.hpp>
#include <expected>
#include <utility>

namespace rock_hero::game::ui
{

// Proves the headless CI path (gate criterion S5): bgfx's Noop backend initializes with no GPU,
// no window, and no platform data, runs frames, resizes, and shuts down cleanly.
TEST_CASE("Render device runs headless frames on the Noop backend", "[ui][surface]")
{
    std::expected<RenderDevice, RenderDeviceError> device = RenderDevice::create(
        RenderDeviceConfig{
            .backend = RenderBackend::Noop,
            .native_window_handle = nullptr,
            .width = 64,
            .height = 64,
            .vsync = false,
        });
    REQUIRE(device.has_value());

    for (int frame = 0; frame < 4; ++frame)
    {
        device->submitClearedFrame();
    }

    device->resize(128, 72);
    device->submitClearedFrame();
}

// The windowed-backend guard must reject a null native handle before touching bgfx, so the
// failure is a typed error rather than a GPU-init crash in a headless environment.
TEST_CASE("Render device rejects a windowed backend without a native handle", "[ui][surface]")
{
    const std::expected<RenderDevice, RenderDeviceError> device = RenderDevice::create(
        RenderDeviceConfig{
            .backend = RenderBackend::Direct3D11,
            .native_window_handle = nullptr,
            .width = 64,
            .height = 64,
            .vsync = true,
        });
    REQUIRE_FALSE(device.has_value());
    CHECK(device.error().code == RenderDeviceErrorCode::MissingNativeWindowHandle);
}

// Pins the production platform table: Windows builds render with the spike-proven Direct3D 11
// backend, never bgfx auto-selection.
TEST_CASE("Default render backend is Direct3D11 on Windows", "[ui][surface]")
{
#if defined(_WIN32)
    CHECK(defaultRenderBackend() == RenderBackend::Direct3D11);
#endif
}

} // namespace rock_hero::game::ui
