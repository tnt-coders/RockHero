#include "noop_render_device.h"

#include <expected>

namespace rock_hero::common::ui
{

// Brings the process's single bgfx instance up on first use. The initial size is arbitrary —
// every case that depends on the backbuffer size resizes first — but non-zero, because a device
// is expected to come up with a real backbuffer.
RenderDevice* sharedNoopDevice()
{
    static std::expected<RenderDevice, RenderDeviceError> g_device = RenderDevice::create(
        RenderDeviceConfig{
            .backend = RenderBackend::Noop,
            .native_window_handle = nullptr,
            .width = 960,
            .height = 540,
            .vsync = false,
        });
    return g_device.has_value() ? &*g_device : nullptr;
}

} // namespace rock_hero::common::ui
