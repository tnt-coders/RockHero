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

// Passes the path through when it names an existing regular file. The probe is non-throwing;
// failures to inspect the file intentionally count as missing, and any probe/read race is caught
// by the read that follows anyway.
[[nodiscard]] std::expected<std::filesystem::path, GameResourcesError> existingResourceFile(
    std::filesystem::path path)
{
    std::error_code probe_error;
    if (!std::filesystem::is_regular_file(path, probe_error))
    {
        return std::unexpected{
            GameResourcesError{GameResourcesErrorCode::MissingResourceFile, path}
        };
    }
    return path;
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
    const common::core::HighwayShaderProgram program, const ShaderStage stage,
    const ShaderBackend backend) const
{
    std::filesystem::path path = m_resources_root / "shaders" / shaderBackendDirectory(backend);
    path /= std::string{shaderStagePrefix(stage)} + "_" +
            std::string{common::core::highwayShaderProgramName(program)} + ".bin";
    return existingResourceFile(std::move(path));
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
    const common::core::HighwayShaderProgram program, const ShaderStage stage,
    const ShaderBackend backend) const
{
    const auto path = shaderPath(program, stage, backend);
    if (!path.has_value())
    {
        return std::unexpected{path.error()};
    }
    return readResourceBytes(*path);
}

// Resolves the mark sheet under resources/highway and reads it through the same typed channel.
std::expected<std::vector<std::byte>, GameResourcesError> GameResources::highwayMarkSheetBytes()
    const
{
    const auto path = existingResourceFile(
        m_resources_root / "highway" / common::core::g_highway_mark_sheet_file_name);
    if (!path.has_value())
    {
        return std::unexpected{path.error()};
    }
    return readResourceBytes(*path);
}

GameResources::GameResources(std::filesystem::path resources_root)
    : m_resources_root(std::move(resources_root))
{}

} // namespace rock_hero::game::core
