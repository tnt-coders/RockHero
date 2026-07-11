#include "resources/game_resources.h"

#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

namespace rock_hero::game::core
{

namespace
{

// Creates a unique empty fixture directory and removes it (and everything in it) on scope exit.
class TempResourcesRoot
{
public:
    TempResourcesRoot()
        : m_root(
              std::filesystem::temp_directory_path() /
              ("rock_hero_game_resources_test_" +
               std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())))
    {
        std::filesystem::create_directories(m_root);
    }

    ~TempResourcesRoot()
    {
        std::error_code cleanup_error;
        std::filesystem::remove_all(m_root, cleanup_error);
    }

    TempResourcesRoot(const TempResourcesRoot&) = delete;
    TempResourcesRoot& operator=(const TempResourcesRoot&) = delete;
    TempResourcesRoot(TempResourcesRoot&&) = delete;
    TempResourcesRoot& operator=(TempResourcesRoot&&) = delete;

    [[nodiscard]] const std::filesystem::path& path() const
    {
        return m_root;
    }

    // Creates an empty file (and its parent directories) under the fixture root.
    void touch(const std::filesystem::path& relative) const
    {
        const std::filesystem::path target = m_root / relative;
        std::filesystem::create_directories(target.parent_path());
        const std::ofstream file{target};
    }

private:
    std::filesystem::path m_root;
};

} // namespace

TEST_CASE("GameResources rejects a missing resources root", "[game-core][resources]")
{
    const TempResourcesRoot fixture;

    const auto resources = GameResources::create(fixture.path() / "does_not_exist");

    REQUIRE_FALSE(resources.has_value());
    if (!resources.has_value())
    {
        CHECK(resources.error().code == GameResourcesErrorCode::MissingResourcesRoot);
        CHECK(resources.error().message.find("does_not_exist") != std::string::npos);
    }
}

TEST_CASE("GameResources resolves shader stage binaries by convention", "[game-core][resources]")
{
    const TempResourcesRoot fixture;
    fixture.touch("shaders/dx11/vs_surface_flat.bin");
    fixture.touch("shaders/dx11/fs_surface_flat.bin");

    const auto resources = GameResources::create(fixture.path());
    REQUIRE(resources.has_value());
    if (resources.has_value())
    {
        const auto vertex = resources->shaderPath(
            GameShaderProgram::SurfaceFlat, ShaderStage::Vertex, ShaderBackend::Direct3D11);
        REQUIRE(vertex.has_value());
        if (vertex.has_value())
        {
            CHECK(*vertex == fixture.path() / "shaders" / "dx11" / "vs_surface_flat.bin");
        }

        const auto fragment = resources->shaderPath(
            GameShaderProgram::SurfaceFlat, ShaderStage::Fragment, ShaderBackend::Direct3D11);
        REQUIRE(fragment.has_value());
        if (fragment.has_value())
        {
            CHECK(*fragment == fixture.path() / "shaders" / "dx11" / "fs_surface_flat.bin");
        }
    }
}

TEST_CASE(
    "GameResources reports a missing shader binary as a typed error", "[game-core][resources]")
{
    const TempResourcesRoot fixture;
    fixture.touch("shaders/dx11/vs_surface_flat.bin");

    const auto resources = GameResources::create(fixture.path());
    REQUIRE(resources.has_value());
    if (resources.has_value())
    {
        const auto fragment = resources->shaderPath(
            GameShaderProgram::SurfaceFlat, ShaderStage::Fragment, ShaderBackend::Direct3D11);
        REQUIRE_FALSE(fragment.has_value());
        if (!fragment.has_value())
        {
            CHECK(fragment.error().code == GameResourcesErrorCode::MissingResourceFile);
            CHECK(fragment.error().message.find("fs_surface_flat.bin") != std::string::npos);
        }
    }
}

} // namespace rock_hero::game::core
