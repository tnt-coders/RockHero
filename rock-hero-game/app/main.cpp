#include <charconv>
#include <cstdint>
#include <cstdio>
#include <expected>
#include <filesystem>
#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>
#include <optional>
#include <print>
#include <rock_hero/common/audio/engine/engine.h>
#include <rock_hero/common/audio/input/live_input_monitor.h>
#include <rock_hero/common/audio/settings/active_device_route.h>
#include <rock_hero/common/audio/settings/audio_config_identity.h>
#include <rock_hero/common/audio/settings/audio_config_store.h>
#include <rock_hero/common/core/shared/application_identity.h>
#include <rock_hero/common/core/shared/cancellation_token.h>
#include <rock_hero/common/core/shared/juce_path.h>
#include <rock_hero/common/core/shared/logger.h>
#include <rock_hero/game/core/library/filesystem_directory_lister.h>
#include <rock_hero/game/core/library/library_index.h>
#include <rock_hero/game/core/library/library_scan.h>
#include <rock_hero/game/core/library/library_scan_roots.h>
#include <rock_hero/game/core/library/null_album_art_generator.h>
#include <rock_hero/game/core/library/rock_song_package_describer.h>
#include <rock_hero/game/core/session/gameplay_session.h>
#include <rock_hero/game/core/settings/game_settings.h>
#include <rock_hero/game/ui/surface/game_shell.h>
#include <string>
#include <string_view>
#include <vector>

namespace rock_hero::game::app
{
namespace
{

// Resolves the rolling game log file under the same "Rock Hero" app-data folder as the editor's
// log, so both products' logs sit side by side where users and developers already look.
[[nodiscard]] std::filesystem::path gameLogFile()
{
    const std::string_view folder_name = common::core::applicationDataFolderName();
    const juce::File log_file =
        juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
            .getChildFile(juce::String{folder_name.data(), folder_name.size()})
            .getChildFile("Rock Hero Game.log");
    return std::filesystem::path{log_file.getFullPathName().toStdString()};
}

// Returns the value following the named argument, or empty when the argument is absent.
[[nodiscard]] std::optional<std::string_view> argumentValue(
    const std::string_view name, const int argc, char** argv)
{
    for (int index = 1; index + 1 < argc; ++index)
    {
        if (std::string_view{argv[index]} == name)
        {
            return std::string_view{argv[index + 1]};
        }
    }
    return std::nullopt;
}

// Returns true when the named flag argument is present.
[[nodiscard]] bool hasFlag(const std::string_view name, const int argc, char** argv)
{
    for (int index = 1; index < argc; ++index)
    {
        if (std::string_view{argv[index]} == name)
        {
            return true;
        }
    }
    return false;
}

// Parses the optional "--smoke-frames <count>" diagnostic argument: a bounded run that exits
// cleanly after the given frame count, used by automated verification and smoke checks.
[[nodiscard]] std::optional<std::uint64_t> smokeFrameLimit(const int argc, char** argv)
{
    const std::optional<std::string_view> count_text = argumentValue("--smoke-frames", argc, argv);
    if (!count_text.has_value())
    {
        return std::nullopt;
    }

    std::uint64_t parsed = 0;
    const std::from_chars_result result =
        std::from_chars(count_text->data(), count_text->data() + count_text->size(), parsed);
    if (result.ec == std::errc{} && parsed > 0)
    {
        return parsed;
    }
    return std::nullopt;
}

// Parses the optional "--dev-package <path>" development argument: the .rock package whose first
// charted arrangement the highway scrolls (plan 25 Phase 3's fixture path; plan 26's library
// replaces it for players).
[[nodiscard]] std::optional<std::filesystem::path> devPackagePath(const int argc, char** argv)
{
    const std::optional<std::string_view> path_text = argumentValue("--dev-package", argc, argv);
    if (!path_text.has_value() || path_text->empty())
    {
        return std::nullopt;
    }
    return std::filesystem::path{*path_text};
}

// Rebuilds a single command-line string from argv for the engine's plugin-scan child check; the
// scanner child identifies itself with one marker token, so simple space joining is sufficient.
[[nodiscard]] std::string joinedCommandLine(const int argc, char** argv)
{
    std::string command_line;
    for (int index = 1; index < argc; ++index)
    {
        if (!command_line.empty())
        {
            command_line += ' ';
        }
        command_line += argv[index];
    }
    return command_line;
}

// Composes the unique per-session scratch directory under per-user app data. The session
// creates and deletes the directory; the composer owns uniqueness (plan 21 Phase 2 contract).
[[nodiscard]] std::filesystem::path makeSessionWorkspaceDirectory()
{
    const std::string_view folder_name = common::core::applicationDataFolderName();
    const juce::File workspace_root =
        juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
            .getChildFile(juce::String{folder_name.data(), folder_name.size()})
            .getChildFile("game-sessions")
            .getChildFile(juce::Uuid{}.toString());
    return std::filesystem::path{workspace_root.getFullPathName().toStdString()};
}

// Scans the song library at startup for the shell's menu: the per-user default Songs folder
// (created on demand) plus any custom roots from settings. A fresh scan each launch is cheap
// because the peek reader never extracts (plan 26 Phase 1); an index cache is a later optimization.
[[nodiscard]] rock_hero::game::core::LibraryIndex scanSongLibrary()
{
    namespace core = rock_hero::game::core;
    namespace common_core = rock_hero::common::core;

    const std::string_view folder_name = common_core::applicationDataFolderName();
    const std::filesystem::path app_data_directory = common_core::pathFromJuceFile(
        juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
            .getChildFile(juce::String{folder_name.data(), folder_name.size()}));

    core::GameSettings settings;
    const std::vector<std::filesystem::path> custom_roots = settings.customScanRoots();
    const std::vector<std::filesystem::path> roots =
        core::resolveLibraryScanRoots(app_data_directory, custom_roots);

    // Create the default Songs folder so the player has somewhere to drop packages.
    if (!roots.empty())
    {
        common_core::juceFileFromPath(roots.front()).createDirectory();
    }

    core::FilesystemDirectoryLister lister;
    core::RockSongPackageDescriber describer;
    core::NullAlbumArtGenerator album_art_generator;
    const common_core::CancellationToken token;
    return core::scanLibrary(roots, lister, describer, album_art_generator, token);
}

} // namespace
} // namespace rock_hero::game::app

// SDL owns the process entry point under loop model L2: a plain portable main() (the game window
// marks SDL's entry-point handling as app-provided) that composes and runs the game shell. JUCE
// runs as a library inside the shell — there is no JUCEApplication in this process. Logging is
// composed here, before the shell, so the frame loop's timing instrumentation (plan 20 Phase 3)
// has a live backend for its whole run; a logging failure is reported and never blocks the game.
// The catch-all keeps exceptions from escaping main (path/format machinery can throw): an
// unhandled escape would terminate without the nonzero exit code automation relies on.
int main(int argc, char** argv)
try
{
    using rock_hero::common::core::Logger;

    // Plugin-file scans relaunch this executable as a short-lived scanner child (the same
    // out-of-process isolation the editor uses); the marker token must be answered before any
    // normal startup work.
    const std::string command_line = rock_hero::game::app::joinedCommandLine(argc, argv);
    if (rock_hero::common::audio::Engine::isPluginScanChildProcessCommandLine(command_line))
    {
        return rock_hero::common::audio::Engine::startPluginScanChildProcess(command_line) ? 0 : 1;
    }

    // The dev flag activates the diagnostics layer (plan 20 Phase 4, 20-Q5: A) and lowers the
    // runtime log level so the per-frame trace instrumentation records.
    const bool dev_mode = rock_hero::game::app::hasFlag("--dev", argc, argv);

    const std::filesystem::path log_file = rock_hero::game::app::gameLogFile();
    const std::expected<void, rock_hero::common::core::LoggerError> logging_result = Logger::init(
        Logger::Config{
            .log_file = log_file,
            .default_level = dev_mode ? Logger::Level::Trace : Logger::Level::Info,
        });
    if (logging_result.has_value())
    {
        RH_LOG_INFO(
            "game.app", "Rock Hero started log_file={:?} dev_mode={}", log_file.string(), dev_mode);
    }
    else
    {
        std::println(
            stderr,
            "rock-hero: logging could not be started; continuing without logging: {}",
            logging_result.error().message);
    }

    // Composition per the decided GameShell watch item (inject from app/): main owns the JUCE
    // runtime, the audio engine, and the gameplay session; the shell receives non-owning
    // pointers and only drives them. Teardown order is the reverse: run() returns (window and
    // GPU device die), the session closes (stops audio, releases the arrangement, deletes its
    // scratch workspace), the engine destructs, and the JUCE guard goes last.
    const juce::ScopedJuceInitialiser_GUI juce_runtime;
    rock_hero::common::audio::Engine audio_engine;

    // The game owns its own audio-config store (its own file, sole writer). Restore the game's own
    // saved input route before the session starts so the calibrate-first gate lands on the route
    // the game's native config selected; an absent route keeps the engine's initialise(1, 2)
    // default. This runs on the message thread, matching the engine's own device init.
    rock_hero::common::audio::AudioConfigStore game_audio_config_store{
        rock_hero::common::audio::gameAudioConfigApplicationName(),
        rock_hero::common::audio::AudioConfigStore::Access::ReadWrite
    };
    if (const std::optional<rock_hero::common::audio::ActiveDeviceRoute> active_device_route =
            game_audio_config_store.activeDeviceRoute();
        active_device_route.has_value() && !active_device_route->serialized_state.empty())
    {
        if (const auto restored =
                audio_engine.restoreSerializedDeviceState(active_device_route->serialized_state);
            !restored.has_value())
        {
            RH_LOG_WARNING(
                "game.app",
                "Stored audio device route could not be restored; using defaults: {}",
                restored.error().message);
        }
    }

    // The engine implements both ILiveInput and IAudioDeviceConfiguration; the store is the
    // swappable IAudioConfigStore& (the game reads and writes its own file, never the editor's).
    rock_hero::common::audio::LiveInputMonitor live_input_monitor{
        audio_engine, audio_engine, game_audio_config_store
    };

    rock_hero::game::core::GameplaySession gameplay_session{
        audio_engine,
        audio_engine,
        audio_engine,
        audio_engine,
        audio_engine,
        audio_engine,
        live_input_monitor
    };

    rock_hero::game::ui::GameShellOptions options{};
    options.frame_limit = rock_hero::game::app::smokeFrameLimit(argc, argv);
    options.dev_package = rock_hero::game::app::devPackagePath(argc, argv);
    options.lefty = rock_hero::game::app::hasFlag("--lefty", argc, argv);
    options.dev_mode = dev_mode;
    options.gameplay_session = &gameplay_session;
    options.session_workspace_directory = rock_hero::game::app::makeSessionWorkspaceDirectory();

    // Normal launch scans the library and opens the song-selection menu; --dev-package keeps the
    // direct-load development path (no menu, auto-plays the given package).
    if (!options.dev_package.has_value())
    {
        options.library = rock_hero::game::app::scanSongLibrary();
        RH_LOG_INFO("game.app", "library scan found {} songs", options.library->entries.size());
    }

    const int exit_code = rock_hero::game::ui::GameShell{}.run(options);
    gameplay_session.close();

    if (logging_result.has_value())
    {
        RH_LOG_INFO("game.app", "Rock Hero shutdown complete exit_code={}", exit_code);
        Logger::shutdown();
    }
    return exit_code;
}
catch (...)
{
    // std::println could itself throw; fputs cannot.
    (void)std::fputs("rock-hero: terminated by an unhandled exception\n", stderr);
    return 1;
}
