/*!
\file game_resources.h
\brief Typed resolver from game resource ids to files under the deployed resources tree.
*/

#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <string>
#include <vector>

namespace rock_hero::game::core
{

/*!
\brief Shader backends the resources tree ships compiled shader binaries for.

The plan-20 gate pinned Direct3D 11 as the only shipped backend for now; adding one later is a
new enumerator plus a build-list edit, never a loading-path redesign (the reason this convention
exists).
*/
enum class ShaderBackend : std::uint8_t
{
    /*! \brief Direct3D 11 shader binaries, deployed under shaders/dx11/. */
    Direct3D11,
};

/*! \brief The two shader stages a bgfx program is linked from. */
enum class ShaderStage : std::uint8_t
{
    /*! \brief Vertex shader binary (vs_*.bin). */
    Vertex,

    /*! \brief Fragment shader binary (fs_*.bin). */
    Fragment,
};

/*!
\brief Shader programs the game ships.

Each enumerator names one committed .sc source pair in rock-hero-game/ui/shaders/ whose compiled
binaries land in the deployed resources tree. The set mirrors the highway reference's programs
(plan 25 § Decisions): plain vertex color, distance-faded color, channel-scheme tinted texture,
and glyph text.
*/
enum class GameShaderProgram : std::uint8_t
{
    /*! \brief Flat vertex-color geometry (board furniture, rails, boxes). */
    Color,

    /*! \brief Vertex color with a Z-ramp alpha fade (beat bars fading toward the horizon). */
    ColorFade,

    /*!
    \brief Atlas-textured quads with the reference channel scheme: texture R multiplies the tint
    color, G adds white highlight, B is the alpha mask — one atlas serves every string color.
    */
    TextureTint,

    /*! \brief Glyph-atlas text (fret numbers, section labels). */
    Glyph,
};

/*! \brief Stable reasons resource resolution can fail. */
enum class GameResourcesErrorCode : std::uint8_t
{
    /*! \brief The resources root directory does not exist next to the executable. */
    MissingResourcesRoot,

    /*! \brief A resolved resource file is absent from the resources tree. */
    MissingResourceFile,

    /*! \brief A resolved resource file exists but could not be read, or is empty. */
    UnreadableResourceFile,
};

/*! \brief Typed boundary error for resource resolution failures. */
struct [[nodiscard]] GameResourcesError
{
    /*! \brief Stable failure reason for program branching. */
    GameResourcesErrorCode code{};

    /*! \brief Human-readable diagnostic naming the missing path, for UI display or logs. */
    std::string message;

    /*!
    \brief Creates an error naming the path that failed to resolve.
    \param error_code Stable failure reason.
    \param path Path the failure is about.
    */
    GameResourcesError(GameResourcesErrorCode error_code, const std::filesystem::path& path);
};

/*!
\brief Resolves typed resource ids to paths under an injected resources root.

This is the one loading seam every game plan resolves packaged assets through (fonts, shaders,
SFX, textures), so later features never invent their own path conventions. The root is injected:
the app composes the executable-relative resources directory, tests compose temp fixtures.
*/
class GameResources
{
public:
    /*!
    \brief Validates the resources root and builds a resolver over it.

    \param resources_root Directory the deployed resources tree lives in.
    \return The resolver, or a typed error when the root directory is missing.
    */
    [[nodiscard]] static std::expected<GameResources, GameResourcesError> create(
        std::filesystem::path resources_root);

    /*!
    \brief Resolves one compiled shader-stage binary.

    \param program Shader program the stage belongs to.
    \param stage Vertex or fragment stage.
    \param backend Backend whose compiled binary to resolve.
    \return Absolute path of the existing binary, or a typed error naming the missing file.
    */
    [[nodiscard]] std::expected<std::filesystem::path, GameResourcesError> shaderPath(
        GameShaderProgram program, ShaderStage stage, ShaderBackend backend) const;

    /*!
    \brief Resolves and reads one compiled shader-stage binary into memory.

    Reading resolved resources lives here rather than in each consumer so every failure — missing
    file, unreadable file, empty file — reports through the same typed error channel naming the
    exact path.

    \param program Shader program the stage belongs to.
    \param stage Vertex or fragment stage.
    \param backend Backend whose compiled binary to read.
    \return The binary's bytes (never empty), or a typed error naming the failing file.
    */
    [[nodiscard]] std::expected<std::vector<std::byte>, GameResourcesError> shaderBytes(
        GameShaderProgram program, ShaderStage stage, ShaderBackend backend) const;

private:
    // Only create() constructs a resolver, after validating the root.
    explicit GameResources(std::filesystem::path resources_root);

    // Validated resources root every resolution is relative to.
    std::filesystem::path m_resources_root;
};

} // namespace rock_hero::game::core
