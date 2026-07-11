#include "surface/bgfx_program.h"

#include <cstdint>

namespace rock_hero::game::ui
{

UniqueBgfxHandle<bgfx::ProgramHandle> createProgramFromBytes(
    const std::span<const std::byte> vertex_bytes, const std::span<const std::byte> fragment_bytes)
{
    if (vertex_bytes.empty() || fragment_bytes.empty())
    {
        return {};
    }

    const UniqueBgfxHandle<bgfx::ShaderHandle> vertex_handle{bgfx::createShader(
        bgfx::copy(vertex_bytes.data(), static_cast<std::uint32_t>(vertex_bytes.size())))};
    const UniqueBgfxHandle<bgfx::ShaderHandle> fragment_handle{bgfx::createShader(
        bgfx::copy(fragment_bytes.data(), static_cast<std::uint32_t>(fragment_bytes.size())))};
    if (!vertex_handle.isValid() || !fragment_handle.isValid())
    {
        return {};
    }

    return UniqueBgfxHandle<bgfx::ProgramHandle>{bgfx::createProgram(
        vertex_handle.get(), fragment_handle.get(), false)};
}

} // namespace rock_hero::game::ui
