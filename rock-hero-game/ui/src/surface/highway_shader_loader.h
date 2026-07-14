/*!
\file highway_shader_loader.h
\brief Loads the shared highway renderer's compiled shaders from the game resource pack.
*/

#pragma once

#include <expected>
#include <rock_hero/common/ui/highway/highway_renderer.h>
#include <rock_hero/game/core/resources/game_resources.h>

namespace rock_hero::game::ui
{

/*!
\brief Reads the five highway programs' compiled stage binaries from the resource pack.

The shared renderer never touches the filesystem (its shader-set seam); this is the game's half
of that contract — the editor preview has its own loader over its deployed resources.

\param resources Resolver over the deployed resource-pack tree.
\return The filled shader set, or the first typed resource error encountered.
*/
[[nodiscard]] std::expected<common::ui::HighwayShaderSet, core::GameResourcesError>
loadHighwayShaderSet(const core::GameResources& resources);

} // namespace rock_hero::game::ui
