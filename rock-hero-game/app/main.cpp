#include <charconv>
#include <cstdint>
#include <cstdio>
#include <expected>
#include <filesystem>
#include <juce_core/juce_core.h>
#include <optional>
#include <print>
#include <rock_hero/common/core/shared/application_identity.h>
#include <rock_hero/common/core/shared/logger.h>
#include <rock_hero/game/ui/surface/game_shell.h>
#include <string_view>

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

    rock_hero::game::ui::GameShellOptions options{};
    options.frame_limit = rock_hero::game::app::smokeFrameLimit(argc, argv);
    options.dev_package = rock_hero::game::app::devPackagePath(argc, argv);
    options.lefty = rock_hero::game::app::hasFlag("--lefty", argc, argv);
    options.dev_mode = dev_mode;
    const int exit_code = rock_hero::game::ui::GameShell{}.run(options);

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
