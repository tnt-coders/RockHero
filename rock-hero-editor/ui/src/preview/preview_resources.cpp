#include "preview/preview_resources.h"

#include <cstddef>
#include <cstring>
#include <juce_core/juce_core.h>
#include <rock_hero/common/core/shared/logger.h>
#include <string>
#include <utility>
#include <vector>

namespace rock_hero::editor::ui
{

namespace
{

// Resolves the deployed resources tree beside the running editor executable.
[[nodiscard]] juce::File resourcesRoot()
{
    return juce::File::getSpecialLocation(juce::File::currentExecutableFile)
        .getParentDirectory()
        .getChildFile("resources");
}

// Reads a whole resource file; empty on failure (the caller decides whether that is fatal).
[[nodiscard]] std::vector<std::byte> readFileBytes(const juce::File& file)
{
    juce::MemoryBlock block;
    if (!file.existsAsFile() || !file.loadFileAsData(block) || block.getSize() == 0)
    {
        return {};
    }
    std::vector<std::byte> bytes(block.getSize());
    std::memcpy(bytes.data(), block.getData(), block.getSize());
    return bytes;
}

// Loads one compiled shader stage; empty on failure with a log naming the file.
[[nodiscard]] std::vector<std::byte> readShaderStage(const std::string& file_name)
{
    const juce::File file =
        resourcesRoot().getChildFile("shaders").getChildFile("dx11").getChildFile(file_name);
    std::vector<std::byte> bytes = readFileBytes(file);
    if (bytes.empty())
    {
        RH_LOG_WARNING(
            "editor.preview",
            "compiled shader missing or unreadable: {:?}",
            file.getFullPathName().toStdString());
    }
    return bytes;
}

} // namespace

std::optional<common::ui::HighwayShaderSet> loadPreviewHighwayShaders()
{
    common::ui::HighwayShaderSet set;

    const auto load_pair = [](const std::string& program) -> common::ui::HighwayShaderPair {
        return common::ui::HighwayShaderPair{
            .vertex = readShaderStage("vs_" + program + ".bin"),
            .fragment = readShaderStage("fs_" + program + ".bin"),
        };
    };

    set.color = load_pair("color");
    set.color_fade = load_pair("color_fade");
    set.texture_tint = load_pair("texture_tint");
    set.glyph = load_pair("glyph");
    set.texture = load_pair("texture");
    set.window_light = load_pair("window_light");

    for (const common::ui::HighwayShaderPair* pair :
         {&set.color,
          &set.color_fade,
          &set.texture_tint,
          &set.glyph,
          &set.texture,
          &set.window_light})
    {
        if (pair->vertex.empty() || pair->fragment.empty())
        {
            return std::nullopt;
        }
    }
    return set;
}

common::ui::HighwayTextureSet loadPreviewHighwayTextures()
{
    const juce::File textures = resourcesRoot().getChildFile("textures").getChildFile("charter");
    common::ui::HighwayTextureSet set;
    set.note_atlas_png = readFileBytes(textures.getChildFile("notes.png"));
    set.inlay_atlas_png = readFileBytes(textures.getChildFile("inlays.png"));
    set.fingering_png = readFileBytes(textures.getChildFile("fingering.png"));
    return set;
}

} // namespace rock_hero::editor::ui
