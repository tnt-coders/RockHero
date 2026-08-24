#pragma once

#include <rock_hero/common/ui/render/render_device.h>

namespace rock_hero::common::ui
{

/*
The one Noop-backed bgfx device this test binary owns.

bgfx is a process singleton, and a ONE-SHOT one: shutdown() leaves its internal
s_renderFrameCalled flag set while clearing the thread index, so the renderFrame-before-init
handshake a second RenderDevice::create performs trips bgfx's own render-thread assert (bgfx.cpp,
BGFX_CHECK_RENDER_THREAD). Every headless case in this binary therefore shares one device rather
than creating its own: ctest gives each Catch2 case its own process, but the agent build helper
runs the executable directly, where all cases share one.

The device is created on first use and destroyed at process exit, which is where bgfx::shutdown
runs. Its backbuffer size is whatever the last case left, so a case that cares must resize it.

\return The device, or null when bgfx refused to come up.
*/
[[nodiscard]] RenderDevice* sharedNoopDevice();

} // namespace rock_hero::common::ui
