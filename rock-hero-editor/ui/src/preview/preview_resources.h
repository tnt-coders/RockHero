/*!
\file preview_resources.h
\brief Loads the 3D preview's deployed shader and texture resources beside the editor executable.
*/

#pragma once

#include <optional>
#include <rock_hero/common/ui/highway/highway_renderer.h>

namespace rock_hero::editor::ui
{

/*!
\brief Reads the shared highway programs' compiled binaries from the editor's resources tree.

The editor's half of the shared renderer's no-filesystem contract: the build deploys the
compiled shaders under resources/shaders/dx11 next to the executable (the same layout the game
uses), and this loads them into the shader-set seam.

\return The filled shader set, or empty when any binary is missing (logged per file).
*/
[[nodiscard]] std::optional<common::ui::HighwayShaderSet> loadPreviewHighwayShaders();

/*!
\brief Reads the shared highway texture assets from the editor's resources tree.

Best-effort: a missing asset leaves its member empty and the renderer falls back to procedural
art, never failing the preview.

\return The texture set with every readable asset filled.
*/
[[nodiscard]] common::ui::HighwayTextureSet loadPreviewHighwayTextures();

} // namespace rock_hero::editor::ui
