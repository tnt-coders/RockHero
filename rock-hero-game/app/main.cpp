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

// Parses the optional "--smoke-frames <count>" diagnostic argument: a bounded run that exits
// cleanly after the given frame count, used by automated verification and smoke checks.
[[nodiscard]] std::optional<std::uint64_t> smokeFrameLimit(const int argc, char** argv)
{
    for (int index = 1; index + 1 < argc; ++index)
    {
        if (std::string_view{argv[index]} != "--smoke-frames")
        {
            continue;
        }

        const std::string_view count_text{argv[index + 1]};
        std::uint64_t parsed = 0;
        const std::from_chars_result result =
            std::from_chars(count_text.data(), count_text.data() + count_text.size(), parsed);
        if (result.ec == std::errc{} && parsed > 0)
        {
            return parsed;
        }
    }
    return std::nullopt;
}

} // namespace
} // namespace rock_hero::game::app

// SDL owns the process entry point under loop model L2: a plain portable main() (the game window
// marks SDL's entry-point handling as app-provided) that composes and runs the game shell. JUCE
// runs as a library inside the shell — there is no JUCEApplication in this process. Logging is
// composed here, before the shell, so the frame loop's timing instrumentation (plan 20 Phase 3)
// has a live backend for its whole run; a logging failure is reported and never blocks the game.
int main(int argc, char** argv)
{
    using rock_hero::common::core::Logger;

    const std::filesystem::path log_file = rock_hero::game::app::gameLogFile();
    const std::expected<void, rock_hero::common::core::LoggerError> logging_result =
        Logger::init(Logger::Config{.log_file = log_file});
    if (logging_result.has_value())
    {
        RH_LOG_INFO("game.app", "Rock Hero started log_file={:?}", log_file.string());
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
    const int exit_code = rock_hero::game::ui::GameShell{}.run(options);

    if (logging_result.has_value())
    {
        RH_LOG_INFO("game.app", "Rock Hero shutdown complete exit_code={}", exit_code);
        Logger::shutdown();
    }
    return exit_code;
}
