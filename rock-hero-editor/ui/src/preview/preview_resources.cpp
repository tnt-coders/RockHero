#include "preview/preview_resources.h"

#include <cstddef>
#include <cstring>
#include <juce_core/juce_core.h>
#include <rock_hero/common/core/highway/highway_resources.h>
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

    // Walk the shared program table instead of spelling program names here: a name that does not
    // match a deployed binary used to read as an empty stage, and a program the preview forgot
    // entirely used to render with a default-constructed handle.
    for (const common::core::HighwayShaderProgram program : common::core::g_highway_shader_programs)
    {
        const std::string name{common::core::highwayShaderProgramName(program)};
        common::ui::HighwayShaderPair pair{
            .vertex = readShaderStage("vs_" + name + ".bin"),
            .fragment = readShaderStage("fs_" + name + ".bin"),
        };
        if (pair.vertex.empty() || pair.fragment.empty())
        {
            return std::nullopt;
        }
        set.at(common::core::indexOf(program)) = std::move(pair);
    }

    return set;
}

common::ui::HighwayTextureSet loadPreviewHighwayTextures()
{
    const juce::File textures = resourcesRoot().getChildFile("textures");
    common::ui::HighwayTextureSet set;
    for (const common::core::HighwayTexture texture : common::core::g_highway_textures)
    {
        const std::string file_name{common::core::highwayTextureFileName(texture)};
        set.at(common::core::indexOf(texture)) =
            readFileBytes(textures.getChildFile(juce::String{file_name}));
    }
    return set;
}

} // namespace rock_hero::editor::ui
