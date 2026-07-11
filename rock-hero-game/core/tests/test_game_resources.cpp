#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cstddef>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <ios>
#include <rock_hero/game/core/resources/game_resources.h>
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

    // Removes the fixture tree on a best-effort basis; cleanup failure must never terminate
    // the test run (even the error_code overload may throw std::bad_alloc).
    ~TempResourcesRoot() noexcept
    {
        try
        {
            std::error_code cleanup_error;
            std::filesystem::remove_all(m_root, cleanup_error);
        }
        catch (...)
        {
            // Deliberately swallowed after reporting: the OS temp cleaner collects strays.
            (void)std::fputs("warning: temp fixture cleanup failed\n", stderr);
        }
    }

    TempResourcesRoot(const TempResourcesRoot&) = delete;
    TempResourcesRoot& operator=(const TempResourcesRoot&) = delete;
    TempResourcesRoot(TempResourcesRoot&&) = delete;
    TempResourcesRoot& operator=(TempResourcesRoot&&) = delete;

    [[nodiscard]] const std::filesystem::path& path() const
    {
        return m_root;
    }

    // Creates a file (and its parent directories) under the fixture root with the given content;
    // empty content produces an empty file.
    void touch(const std::filesystem::path& relative, const std::string& content = {}) const
    {
        const std::filesystem::path target = m_root / relative;
        std::filesystem::create_directories(target.parent_path());
        std::ofstream file{target, std::ios::binary};
        REQUIRE(file.is_open());
        file << content;
    }

private:
    std::filesystem::path m_root;
};

} // namespace

TEST_CASE("GameResources rejects a missing resources root", "[core][resources]")
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

TEST_CASE("GameResources resolves shader stage binaries by convention", "[core][resources]")
{
    const TempResourcesRoot fixture;
    fixture.touch("shaders/dx11/vs_color.bin");
    fixture.touch("shaders/dx11/fs_color.bin");

    const auto resources = GameResources::create(fixture.path());
    REQUIRE(resources.has_value());
    if (resources.has_value())
    {
        const auto vertex = resources->shaderPath(
            GameShaderProgram::Color, ShaderStage::Vertex, ShaderBackend::Direct3D11);
        REQUIRE(vertex.has_value());
        if (vertex.has_value())
        {
            CHECK(*vertex == fixture.path() / "shaders" / "dx11" / "vs_color.bin");
        }

        const auto fragment = resources->shaderPath(
            GameShaderProgram::Color, ShaderStage::Fragment, ShaderBackend::Direct3D11);
        REQUIRE(fragment.has_value());
        if (fragment.has_value())
        {
            CHECK(*fragment == fixture.path() / "shaders" / "dx11" / "fs_color.bin");
        }
    }
}

TEST_CASE("GameResources reports a missing shader binary as a typed error", "[core][resources]")
{
    const TempResourcesRoot fixture;
    fixture.touch("shaders/dx11/vs_color.bin");

    const auto resources = GameResources::create(fixture.path());
    REQUIRE(resources.has_value());
    if (resources.has_value())
    {
        const auto fragment = resources->shaderPath(
            GameShaderProgram::Color, ShaderStage::Fragment, ShaderBackend::Direct3D11);
        REQUIRE_FALSE(fragment.has_value());
        if (!fragment.has_value())
        {
            CHECK(fragment.error().code == GameResourcesErrorCode::MissingResourceFile);
            CHECK(fragment.error().message.find("fs_color.bin") != std::string::npos);
        }
    }
}

TEST_CASE("GameResources reads shader binary bytes", "[core][resources]")
{
    const TempResourcesRoot fixture;
    fixture.touch("shaders/dx11/vs_color.bin", "VSH\x03");

    const auto resources = GameResources::create(fixture.path());
    REQUIRE(resources.has_value());
    if (resources.has_value())
    {
        const auto bytes = resources->shaderBytes(
            GameShaderProgram::Color, ShaderStage::Vertex, ShaderBackend::Direct3D11);
        REQUIRE(bytes.has_value());
        if (bytes.has_value())
        {
            REQUIRE(bytes->size() == 4);
            CHECK((*bytes)[0] == std::byte{'V'});
            CHECK((*bytes)[3] == std::byte{0x03});
        }
    }
}

TEST_CASE("GameResources reports an empty shader binary as a typed error", "[core][resources]")
{
    const TempResourcesRoot fixture;
    fixture.touch("shaders/dx11/vs_color.bin");

    const auto resources = GameResources::create(fixture.path());
    REQUIRE(resources.has_value());
    if (resources.has_value())
    {
        const auto bytes = resources->shaderBytes(
            GameShaderProgram::Color, ShaderStage::Vertex, ShaderBackend::Direct3D11);
        REQUIRE_FALSE(bytes.has_value());
        if (!bytes.has_value())
        {
            CHECK(bytes.error().code == GameResourcesErrorCode::UnreadableResourceFile);
            CHECK(bytes.error().message.find("vs_color.bin") != std::string::npos);
        }
    }
}

} // namespace rock_hero::game::core
