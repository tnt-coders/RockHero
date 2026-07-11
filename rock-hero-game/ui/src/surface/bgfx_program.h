/*!
\file bgfx_program.h
\brief Links bgfx shader programs from compiled stage binaries.
*/

#pragma once

#include "surface/bgfx_handle.h"

#include <bgfx/bgfx.h>
#include <cstddef>
#include <span>

namespace rock_hero::game::ui
{

/*!
\brief Links a shader program from compiled vertex and fragment binaries.

The stage handles are created here and deliberately NOT flagged for consumption by
bgfx::createProgram (the project rule: bgfx consumes stage handles on some failure paths but not
others, so consuming flags are never used); on success the program holds its own references and
the local stage handles are released on return.

\param vertex_bytes Compiled vertex-stage binary.
\param fragment_bytes Compiled fragment-stage binary.
\return The linked program, or an invalid handle when a stage or the link is rejected.
*/
[[nodiscard]] UniqueBgfxHandle<bgfx::ProgramHandle> createProgramFromBytes(
    std::span<const std::byte> vertex_bytes, std::span<const std::byte> fragment_bytes);

} // namespace rock_hero::game::ui
