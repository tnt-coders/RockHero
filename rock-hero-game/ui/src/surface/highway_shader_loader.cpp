#include "surface/highway_shader_loader.h"

#include <rock_hero/common/core/highway/highway_resources.h>
#include <utility>

namespace rock_hero::game::ui
{

std::expected<common::ui::HighwayShaderSet, core::GameResourcesError> loadHighwayShaderSet(
    const core::GameResources& resources)
{
    common::ui::HighwayShaderSet set;

    // Walk the shared program table rather than naming programs here, so a new highway program is
    // one row in common/core and needs no edit in this loader.
    for (const common::core::HighwayShaderProgram program : common::core::g_highway_shader_programs)
    {
        auto vertex = resources.shaderBytes(
            program, core::ShaderStage::Vertex, core::ShaderBackend::Direct3D11);
        if (!vertex.has_value())
        {
            return std::unexpected{vertex.error()};
        }
        auto fragment = resources.shaderBytes(
            program, core::ShaderStage::Fragment, core::ShaderBackend::Direct3D11);
        if (!fragment.has_value())
        {
            return std::unexpected{fragment.error()};
        }

        set.at(common::core::indexOf(program)) = common::ui::HighwayShaderPair{
            .vertex = std::move(*vertex),
            .fragment = std::move(*fragment),
        };
    }

    return set;
}

} // namespace rock_hero::game::ui
