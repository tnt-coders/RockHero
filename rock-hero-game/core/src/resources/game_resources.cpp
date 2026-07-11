#include "resources/game_resources.h"

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
            return "dx11";
    }

    return "dx11";
}

// bgfx-conventional stage prefix for compiled shader file names.
[[nodiscard]] std::string_view shaderStagePrefix(const ShaderStage stage)
{
    switch (stage)
    {
        case ShaderStage::Vertex:
            return "vs";
        case ShaderStage::Fragment:
            return "fs";
    }

    return "vs";
}

// Base name shared by a program's committed .sc sources and its compiled binaries.
[[nodiscard]] std::string_view shaderProgramName(const GameShaderProgram program)
{
    switch (program)
    {
        case GameShaderProgram::SurfaceFlat:
            return "surface_flat";
    }

    return "surface_flat";
}

} // namespace

GameResourcesError::GameResourcesError(
    const GameResourcesErrorCode error_code, const std::filesystem::path& path)
    : code(error_code)
{
    switch (error_code)
    {
        case GameResourcesErrorCode::MissingResourcesRoot:
            message = "Game resources directory not found: " + path.string();
            return;
        case GameResourcesErrorCode::MissingResourceFile:
            message = "Game resource file not found: " + path.string();
            return;
    }

    message = "Game resource resolution failed: " + path.string();
}

std::expected<GameResources, GameResourcesError> GameResources::create(
    std::filesystem::path resources_root)
{
    std::error_code probe_error;
    if (!std::filesystem::is_directory(resources_root, probe_error))
    {
        return std::unexpected(
            GameResourcesError{GameResourcesErrorCode::MissingResourcesRoot, resources_root});
    }

    return GameResources{std::move(resources_root)};
}

std::expected<std::filesystem::path, GameResourcesError> GameResources::shaderPath(
    const GameShaderProgram program, const ShaderStage stage, const ShaderBackend backend) const
{
    std::filesystem::path path = m_resources_root / "shaders" / shaderBackendDirectory(backend);
    path /= std::string{shaderStagePrefix(stage)} + "_" + std::string{shaderProgramName(program)} +
            ".bin";

    std::error_code probe_error;
    if (!std::filesystem::is_regular_file(path, probe_error))
    {
        return std::unexpected(
            GameResourcesError{GameResourcesErrorCode::MissingResourceFile, path});
    }

    return path;
}

GameResources::GameResources(std::filesystem::path resources_root)
    : m_resources_root(std::move(resources_root))
{}

} // namespace rock_hero::game::core
