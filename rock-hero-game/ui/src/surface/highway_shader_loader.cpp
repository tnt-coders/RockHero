#include "surface/highway_shader_loader.h"

#include <cstddef>
#include <utility>
#include <vector>

namespace rock_hero::game::ui
{

std::expected<common::ui::HighwayShaderSet, core::GameResourcesError> loadHighwayShaderSet(
    const core::GameResources& resources)
{
    common::ui::HighwayShaderSet set;

    const auto load_pair = [&resources](const core::GameShaderProgram program)
        -> std::expected<common::ui::HighwayShaderPair, core::GameResourcesError> {
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
        return common::ui::HighwayShaderPair{
            .vertex = std::move(*vertex), .fragment = std::move(*fragment)
        };
    };

    auto color = load_pair(core::GameShaderProgram::Color);
    if (!color.has_value())
    {
        return std::unexpected{color.error()};
    }
    auto color_fade = load_pair(core::GameShaderProgram::ColorFade);
    if (!color_fade.has_value())
    {
        return std::unexpected{color_fade.error()};
    }
    auto texture_tint = load_pair(core::GameShaderProgram::TextureTint);
    if (!texture_tint.has_value())
    {
        return std::unexpected{texture_tint.error()};
    }
    auto glyph = load_pair(core::GameShaderProgram::Glyph);
    if (!glyph.has_value())
    {
        return std::unexpected{glyph.error()};
    }
    auto texture = load_pair(core::GameShaderProgram::Texture);
    if (!texture.has_value())
    {
        return std::unexpected{texture.error()};
    }

    set.color = std::move(*color);
    set.color_fade = std::move(*color_fade);
    set.texture_tint = std::move(*texture_tint);
    set.glyph = std::move(*glyph);
    set.texture = std::move(*texture);
    return set;
}

} // namespace rock_hero::game::ui
