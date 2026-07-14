/*!
\file rock_hero_game.h
\brief Game composition root: owns the window, render device, resources, and content object.
*/

#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <rock_hero/game/core/library/library_index.h>
#include <rock_hero/game/ui/surface/sdl3_application.h>

namespace rock_hero::game::core
{
class GameplaySession;
} // namespace rock_hero::game::core

namespace rock_hero::game::ui
{

/*!
\brief The game process's composition root: the concrete \ref SDL3Application.

RockHeroGame is the game's counterpart of the editor's `RockHeroEditor`: the composition-root
application object that owns the OS window, the bgfx render device, the resource pack, and the
\ref Game content object, and holds the injected gameplay session and scanned library. The
framework base (\ref SDL3Application) owns the frame loop, the JUCE-message drain, and pacing; this
subclass fills the loop's hooks — \ref onInit builds the window/device/resources/content and wires
the startup dev-package or library path, \ref onInput and \ref onFrame drive one frame, and
\ref onShutdown releases the render stack in teardown order.

Composition (the JUCE runtime, the audio engine, the gameplay session, and the scanned library)
stays in `main()` and is injected through \ref Config, exactly as the editor composes its engine in
`main` and injects it into the window and content. The gameplay session is non-owning and must
outlive the run.
*/
class RockHeroGame : public SDL3Application
{
public:
    /*!
    \brief Composition inputs for one game run: the loop's smoke hook plus injected collaborators.

    The former GameShellOptions bag, split by owner: \ref frame_limit is loop state (handed to the
    \ref SDL3Application base), and the rest describes the content the composition root builds — the
    dev flags, the optional development package, the session workspace, the injected session, and
    the optional scanned library that opens the menu.
    */
    struct Config
    {
        /*!
        \brief When set, the loop exits cleanly after this many frames.

        Diagnostic hook for smoke runs and automated verification; normal runs leave it empty and
        exit through the window's quit request.
        */
        std::optional<std::uint64_t> frame_limit;

        /*! \brief True activates the dev-diagnostics layer (overlay, hot-reload, section seeks). */
        bool dev_mode{false};

        /*! \brief True mirrors the highway for left-handed display. */
        bool lefty{false};

        /*!
        \brief When set, loads this package at startup and scrolls its first charted arrangement.

        The development direct-load path; empty means the menu (when a \ref library is present) or
        an empty board drives the display instead.
        */
        std::optional<std::filesystem::path> dev_package;

        /*! \brief Session-private scratch directory the gameplay session extracts the package into. */
        std::filesystem::path session_workspace_directory;

        /*!
        \brief Non-owning gameplay session composed by the app; null runs without audio.

        The caller owns lifetime: the session must outlive the run and be closed by the caller after
        \ref run returns.
        */
        core::GameplaySession* gameplay_session{nullptr};

        /*!
        \brief When set, opens the song-selection menu over this scanned library.

        Empty, or with \ref dev_package set, skips the menu and runs the direct-load path.
        */
        std::optional<core::LibraryIndex> library;
    };

    /*!
    \brief Constructs the composition root; defers window/device/content creation to \ref run.
    \param config Run options and injected collaborators; see \ref Config.
    */
    explicit RockHeroGame(Config config);

    RockHeroGame(const RockHeroGame&) = delete;
    RockHeroGame& operator=(const RockHeroGame&) = delete;
    RockHeroGame(RockHeroGame&&) = delete;
    RockHeroGame& operator=(RockHeroGame&&) = delete;

    /*! \brief Declared for the pimpl's incomplete type; the render stack is torn down in onShutdown. */
    ~RockHeroGame() override;

protected:
    // SDL3Application hooks; see the base for the loop that drives them.
    [[nodiscard]] std::optional<int> onInit() override;
    [[nodiscard]] FrameControl onInput() override;
    [[nodiscard]] FrameTiming onFrame(
        const std::optional<core::FramePacingSummary>& pacing_summary) override;
    void onShutdown() override;

private:
    // The render/content stack built in onInit and released in onShutdown; its members' declaration
    // order encodes the teardown contract (content and scene handles before the bgfx device, the
    // device before the window). Held behind a pointer so this public header stays free of the
    // private window/device/content types.
    struct Impl;

    // Content and collaborators the composition root holds until onInit consumes them into Impl.
    bool m_dev_mode{false};
    bool m_lefty{false};
    std::optional<std::filesystem::path> m_dev_package;
    std::filesystem::path m_session_workspace_directory;
    core::GameplaySession* m_gameplay_session{nullptr};
    std::optional<core::LibraryIndex> m_library;

    std::unique_ptr<Impl> m_impl;
};

} // namespace rock_hero::game::ui
