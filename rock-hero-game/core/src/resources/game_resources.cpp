#include "resources/game_resources.h"

#include <cstring>
#include <fstream>
#include <ios>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

namespace rock_hero::game::core
{

namespace
{

// Directory names under <root>/shaders/ per backend; mirrors bgfx's conventional profile names.
[[nodiscard]] std::string_view shaderBackendDirectory(const ShaderBackend backend)
{
    switch (backend)
    {
        case ShaderBackend::Direct3D11:
        {
            return "dx11";
        }
    }

    return "dx11";
}

// bgfx-conventional stage prefix for compiled shader file names.
[[nodiscard]] std::string_view shaderStagePrefix(const ShaderStage stage)
{
    switch (stage)
    {
        case ShaderStage::Vertex:
        {
            return "vs";
        }
        case ShaderStage::Fragment:
        {
            return "fs";
        }
    }

    return "vs";
}

// Base name shared by a program's committed .sc sources and its compiled binaries.
[[nodiscard]] std::string_view shaderProgramName(const GameShaderProgram program)
{
    switch (program)
    {
        case GameShaderProgram::Color:
        {
            return "color";
        }
        case GameShaderProgram::ColorFade:
        {
            return "color_fade";
        }
        case GameShaderProgram::TextureTint:
        {
            return "texture_tint";
        }
        case GameShaderProgram::Glyph:
        {
            return "glyph";
        }
        case GameShaderProgram::Texture:
        {
            return "texture";
        }
    }

    return "color";
}

// Path of a texture asset relative to <root>/textures/. The charter/ subtree carries the
// reference assets adapted from Charter (BSD 3-Clause; LICENSE.txt deploys alongside).
[[nodiscard]] std::string_view textureRelativePath(const GameTexture texture)
{
    switch (texture)
    {
        case GameTexture::HighwayNotes:
        {
            return "charter/notes.png";
        }
        case GameTexture::HighwayInlays:
        {
            return "charter/inlays.png";
        }
        case GameTexture::HighwayFingering:
        {
            return "charter/fingering.png";
        }
    }

    return "charter/notes.png";
}

} // namespace

GameResourcesError::GameResourcesError(
    const GameResourcesErrorCode error_code, const std::filesystem::path& path)
    : code(error_code)
{
    switch (error_code)
    {
        case GameResourcesErrorCode::MissingResourcesRoot:
        {
            message = "Game resources directory not found: " + path.string();
            return;
        }
        case GameResourcesErrorCode::MissingResourceFile:
        {
            message = "Game resource file not found: " + path.string();
            return;
        }
        case GameResourcesErrorCode::UnreadableResourceFile:
        {
            message = "Game resource file is empty or unreadable: " + path.string();
            return;
        }
    }

    message = "Game resource resolution failed: " + path.string();
}

std::expected<GameResources, GameResourcesError> GameResources::create(
    std::filesystem::path resources_root)
{
    // The error_code overload is chosen so probe failures never throw; any failure to inspect the
    // root (permissions, unreachable volume) deliberately reports as the same missing-root error.
    std::error_code probe_error;
    if (!std::filesystem::is_directory(resources_root, probe_error))
    {
        return std::unexpected{
            GameResourcesError{GameResourcesErrorCode::MissingResourcesRoot, resources_root}
        };
    }

    return GameResources{std::move(resources_root)};
}

std::expected<std::filesystem::path, GameResourcesError> GameResources::shaderPath(
    const GameShaderProgram program, const ShaderStage stage, const ShaderBackend backend) const
{
    std::filesystem::path path = m_resources_root / "shaders" / shaderBackendDirectory(backend);
    path /= std::string{shaderStagePrefix(stage)} + "_" + std::string{shaderProgramName(program)} +
            ".bin";

    // Non-throwing probe; failures to inspect the file intentionally count as missing, and any
    // probe/read race is caught by the read in shaderBytes anyway.
    std::error_code probe_error;
    if (!std::filesystem::is_regular_file(path, probe_error))
    {
        return std::unexpected{
            GameResourcesError{GameResourcesErrorCode::MissingResourceFile, path}
        };
    }

    return path;
}

namespace
{

// Reads a whole resource file, rejecting empty files so consumers may assume non-empty bytes
// (bgfx asserts on zero-length shader blobs rather than returning an error).
[[nodiscard]] std::expected<std::vector<std::byte>, GameResourcesError> readResourceBytes(
    const std::filesystem::path& path)
{
    std::ifstream file{path, std::ios::binary | std::ios::ate};
    if (!file)
    {
        return std::unexpected{
            GameResourcesError{GameResourcesErrorCode::UnreadableResourceFile, path}
        };
    }

    const std::streamsize size = file.tellg();
    if (size <= 0)
    {
        return std::unexpected{
            GameResourcesError{GameResourcesErrorCode::UnreadableResourceFile, path}
        };
    }

    // Read into chars (the stream's element type), then widen to std::byte with one memcpy —
    // sidestepping the byte-pointer reinterpret_cast a direct read into std::byte storage needs.
    std::vector<char> contents(static_cast<std::size_t>(size));
    file.seekg(0);
    if (!file.read(contents.data(), size))
    {
        return std::unexpected{
            GameResourcesError{GameResourcesErrorCode::UnreadableResourceFile, path}
        };
    }

    std::vector<std::byte> bytes(contents.size());
    std::memcpy(bytes.data(), contents.data(), contents.size());
    return bytes;
}

} // namespace

std::expected<std::vector<std::byte>, GameResourcesError> GameResources::shaderBytes(
    const GameShaderProgram program, const ShaderStage stage, const ShaderBackend backend) const
{
    const auto path = shaderPath(program, stage, backend);
    if (!path.has_value())
    {
        return std::unexpected{path.error()};
    }
    return readResourceBytes(*path);
}

std::expected<std::vector<std::byte>, GameResourcesError> GameResources::textureBytes(
    const GameTexture texture) const
{
    const std::filesystem::path path = m_resources_root / "textures" / textureRelativePath(texture);

    std::error_code probe_error;
    if (!std::filesystem::is_regular_file(path, probe_error))
    {
        return std::unexpected{
            GameResourcesError{GameResourcesErrorCode::MissingResourceFile, path}
        };
    }
    return readResourceBytes(path);
}

GameResources::GameResources(std::filesystem::path resources_root)
    : m_resources_root(std::move(resources_root))
{}

} // namespace rock_hero::game::core
