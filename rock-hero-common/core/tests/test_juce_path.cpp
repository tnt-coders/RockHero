#include <catch2/catch_test_macros.hpp>
#include <rock_hero/common/core/shared/juce_path.h>
#include <string>

namespace rock_hero::common::core
{

// Verifies the shared JUCE path bridge round-trips paths through its generic UTF-8 form,
// including non-ASCII text. The fixture bytes are appended programmatically so this source
// file stays pure ASCII and no compiler source-charset assumption can corrupt them.
TEST_CASE("JUCE path helpers roundtrip paths through UTF-8", "[core][juce-path]")
{
    std::u8string name{u8"Rock Hero/Pro"};
    name.push_back(char8_t{0xC4}); // U+0135 latin small j with circumflex,
    name.push_back(char8_t{0xB5}); // as its two UTF-8 bytes.
    name += u8"ect File.rhp";
    const std::filesystem::path path{name};

    const juce::String path_text = juceStringFromPath(path);
    CHECK(pathFromJuceString(path_text) == path);
}

#if JUCE_WINDOWS
// Platform-specific test asserting platform behavior (allowed per plan 33's guiding
// principle): a native wide drive-letter path must survive the UTF-8 bridge unchanged.
TEST_CASE("JUCE path helpers roundtrip native wide Windows paths", "[core][juce-path]")
{
    std::wstring native{L"C:\\Rock Hero\\Project Fil"};
    native.push_back(wchar_t{0x00E9}); // U+00E9 latin small e with acute.
    native += L".rhp";
    const std::filesystem::path path{native};

    const juce::String path_text = juceStringFromPath(path);
    CHECK(pathFromJuceString(path_text) == path);

    const juce::File file = juceFileFromPath(path);
    CHECK(pathFromJuceFile(file) == path);
}
#endif

} // namespace rock_hero::common::core
