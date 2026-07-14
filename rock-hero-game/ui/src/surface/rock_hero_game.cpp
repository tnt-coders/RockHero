#include "surface/rock_hero_game.h"

#include "game/game.h"
#include "surface/game_window.h"
#include "surface/highway_shader_loader.h"

#include <SDL3/SDL.h>
#include <chrono>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <memory>
#include <optional>
#include <print>
#include <rock_hero/common/core/shared/logger.h>
#include <rock_hero/common/ui/highway/highway_renderer.h>
#include <rock_hero/common/ui/render/render_device.h>
#include <rock_hero/game/core/frame_clock/frame_clock.h>
#include <rock_hero/game/core/resources/game_resources.h>
#include <string>
#include <string_view>
#include <utility>

namespace rock_hero::game::ui
{

namespace
{

// Initial window size in logical coordinates; plan 26's video settings will make this a choice.
constexpr std::uint32_t g_initial_window_width = 1280;
constexpr std::uint32_t g_initial_window_height = 720;

// Resolves the deployed resource-pack root next to the executable — the one loading seam
// packaged assets come through (plan 20 Phase 2).
[[nodiscard]] std::optional<core::GameResources> makeGameResources()
{
    // SDL documents the base path as UTF-8 and as null on failure: the null must be caught before
    // path construction (UB), and the bytes must be decoded as UTF-8 explicitly — MSVC's narrow
    // std::filesystem::path constructor decodes via the system codepage, which corrupts
    // non-ASCII install paths.
    const char* base_path = SDL_GetBasePath();
    if (base_path == nullptr)
    {
        std::println(stderr, "rock-hero: SDL_GetBasePath failed: {}", SDL_GetError());
        return std::nullopt;
    }
    const std::string_view base_path_bytes{base_path};
    const std::u8string base_path_utf8(base_path_bytes.begin(), base_path_bytes.end());
    const std::filesystem::path resources_root =
        std::filesystem::path{base_path_utf8} / "resources";

    std::expected<core::GameResources, core::GameResourcesError> resources =
        core::GameResources::create(resources_root);
    if (!resources.has_value())
    {
        std::println(stderr, "rock-hero: {}", resources.error().message);
        return std::nullopt;
    }
    return std::move(*resources);
}

} // namespace

// The render/content stack built in onInit. Declaration order is the teardown contract, in reverse:
// the content (and the bgfx handles its renderer owns) dies first, then the resources, then the
// device (bgfx::shutdown), and finally the window it renders into. Game is non-movable, so it is
// constructed in place from the already-built renderer rather than moved in.
struct RockHeroGame::Impl
{
    Impl(
        GameWindow window_in, common::ui::RenderDevice device_in, core::GameResources resources_in,
        common::ui::HighwayRenderer renderer_in, Game::Config game_config)
        : window(std::move(window_in))
        , device(std::move(device_in))
        , resources(std::move(resources_in))
        , game(std::move(renderer_in), std::move(game_config))
    {}

    GameWindow window;
    common::ui::RenderDevice device;
    core::GameResources resources;
    Game game;
};

RockHeroGame::RockHeroGame(Config config)
    : SDL3Application(config.frame_limit)
    , m_dev_mode(config.dev_mode)
    , m_lefty(config.lefty)
    , m_dev_package(std::move(config.dev_package))
    , m_session_workspace_directory(std::move(config.session_workspace_directory))
    , m_gameplay_session(config.gameplay_session)
    , m_library(std::move(config.library))
{}

RockHeroGame::~RockHeroGame() = default;

std::optional<int> RockHeroGame::onInit()
{
    std::expected<GameWindow, GameWindowError> window = GameWindow::create(
        "Rock Hero", PixelSize{.width = g_initial_window_width, .height = g_initial_window_height});
    if (!window.has_value())
    {
        // The logging backend is composed in main before this runs, but startup failures also
        // report on stderr (visible when launched from a terminal) plus the exit code.
        std::println(stderr, "rock-hero: {}", window.error().message);
        return 1;
    }

    const PixelSize initial_size = window->pixelSize();
    std::expected<common::ui::RenderDevice, common::ui::RenderDeviceError> device =
        common::ui::RenderDevice::create(
            common::ui::RenderDeviceConfig{
                .backend = common::ui::defaultRenderBackend(),
                .native_window_handle = window->nativeWindowHandle(),
                .width = initial_size.width,
                .height = initial_size.height,
                .vsync = true,
                .debug = m_dev_mode,
            });
    if (!device.has_value())
    {
        std::println(stderr, "rock-hero: {}", device.error().message);
        return 1;
    }

    // Scene renderers own bgfx handles, so they are declared after the device and destroyed
    // first (all handles die before bgfx::shutdown — the wrapper's ordering contract).
    std::optional<core::GameResources> resources = makeGameResources();
    if (!resources.has_value())
    {
        return 1;
    }
    const std::expected<common::ui::HighwayShaderSet, core::GameResourcesError> highway_shaders =
        loadHighwayShaderSet(*resources);
    if (!highway_shaders.has_value())
    {
        std::println(stderr, "rock-hero: {}", highway_shaders.error().message);
        return 1;
    }
    // Texture assets are best-effort: a missing file falls back to procedural art in the
    // renderer, never a startup failure.
    common::ui::HighwayTextureSet highway_textures;
    auto note_atlas = resources->textureBytes(core::GameTexture::HighwayNotes);
    if (note_atlas.has_value())
    {
        highway_textures.note_atlas_png = std::move(*note_atlas);
    }
    else
    {
        RH_LOG_WARNING("game.highway", "{}", note_atlas.error().message);
    }
    auto inlay_atlas = resources->textureBytes(core::GameTexture::HighwayInlays);
    if (inlay_atlas.has_value())
    {
        highway_textures.inlay_atlas_png = std::move(*inlay_atlas);
    }
    else
    {
        RH_LOG_WARNING("game.highway", "{}", inlay_atlas.error().message);
    }
    auto fingering = resources->textureBytes(core::GameTexture::HighwayFingering);
    if (fingering.has_value())
    {
        highway_textures.fingering_png = std::move(*fingering);
    }
    else
    {
        RH_LOG_WARNING("game.highway", "{}", fingering.error().message);
    }
    std::expected<common::ui::HighwayRenderer, common::ui::HighwayRendererError> renderer =
        common::ui::HighwayRenderer::create(*highway_shaders, highway_textures);
    if (!renderer.has_value())
    {
        std::println(stderr, "rock-hero: {}", renderer.error().message);
        return 1;
    }

    // All fallible setup is done; hand the built pieces plus the injected session/library to the
    // content object (its construction cannot fail) and assemble the render/content stack.
    m_impl = std::make_unique<Impl>(
        std::move(*window),
        std::move(*device),
        std::move(*resources),
        std::move(*renderer),
        Game::Config{
            .dev_mode = m_dev_mode,
            .lefty = m_lefty,
            .dev_package = std::move(m_dev_package),
            .session_workspace_directory = std::move(m_session_workspace_directory),
            .gameplay_session = m_gameplay_session,
            .library = std::move(m_library),
        });
    return std::nullopt;
}

SDL3Application::FrameControl RockHeroGame::onInput()
{
    const GameWindowEvents events = m_impl->window.pollEvents();
    if (events.quit_requested)
    {
        return FrameControl::Quit;
    }
    if (events.pixel_size_changed.has_value())
    {
        m_impl->device.resize(events.pixel_size_changed->width, events.pixel_size_changed->height);
    }
    m_impl->game.handleWindowEvents(events);
    return FrameControl::Continue;
}

SDL3Application::FrameTiming RockHeroGame::onFrame(
    const std::optional<core::FramePacingSummary>& pacing_summary)
{
    // One steady-clock stamp at the start of frame building anchors everything this frame draws;
    // Game reads the clock snapshot once from it so every drawable shares one coherent song time.
    // The sanctioned song-time path stays live end to end; song time never comes from wall clock
    // or frame counts (architecture.md "Timing and Latency").
    const std::chrono::nanoseconds frame_sample_time =
        std::chrono::steady_clock::now().time_since_epoch();
    const core::FrameClockSample frame_sample = m_impl->game.update(frame_sample_time);

    m_impl->game.render(m_impl->device, pacing_summary);

    m_impl->device.submitFrame();
    return FrameTiming{
        .sample = frame_sample,
        .cpu_frame_time = m_impl->device.lastCpuFrameTime(),
    };
}

void RockHeroGame::onShutdown()
{
    // Tear down the render/content stack when run() returns — before main closes the injected
    // session and destroys the engine. Impl's member order gives the teardown sequence (content
    // and bgfx handles, then the device, then the window). The game never opens plugin editor
    // windows (the only engine surface that would need the windowing stack alive), so releasing
    // the window here while main's JUCE guard still lives is safe.
    m_impl.reset();
}

} // namespace rock_hero::game::ui
