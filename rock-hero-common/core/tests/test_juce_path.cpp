#include <catch2/catch_test_macros.hpp>
#include <rock_hero/common/core/juce_path.h>

namespace rock_hero::common::core
{

// Verifies the shared JUCE path bridge preserves native path text in both directions.
TEST_CASE("JUCE path helpers roundtrip native paths", "[core][juce-path]")
{
#if JUCE_WINDOWS
    const std::filesystem::path path{L"C:\\Rock Hero\\Project File.rhp"};
#else
    const std::filesystem::path path{"/tmp/Rock Hero/Project File.rhp"};
#endif

    const juce::String path_text = juceStringFromPath(path);
    CHECK(pathFromJuceString(path_text) == path);

    const juce::File file = juceFileFromPath(path);
    CHECK(pathFromJuceFile(file) == path);
}

} // namespace rock_hero::common::core
