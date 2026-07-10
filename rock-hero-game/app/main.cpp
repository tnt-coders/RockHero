#include <charconv>
#include <cstdint>
#include <optional>
#include <rock_hero/game/ui/surface/game_shell.h>
#include <string_view>

namespace rock_hero::game::app
{
namespace
{

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
// runs as a library inside the shell — there is no JUCEApplication in this process.
int main(int argc, char** argv)
{
    rock_hero::game::ui::GameShellOptions options{};
    options.frame_limit = rock_hero::game::app::smokeFrameLimit(argc, argv);
    return rock_hero::game::ui::GameShell{}.run(options);
}
