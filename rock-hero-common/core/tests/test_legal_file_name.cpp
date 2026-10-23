#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <rock_hero/common/core/shared/legal_file_name.h>
#include <string>

namespace rock_hero::common::core
{

// A path separator in the text becomes "_", never "-", so it cannot read as the " - " separator.
TEST_CASE("legalFileName replaces a slash with an underscore", "[core][legal-file-name]")
{
    CHECK(legalFileName("AC/DC - Back in Black") == "AC_DC - Back in Black");
}

// Ordinary punctuation every OS accepts passes untouched.
TEST_CASE("legalFileName keeps legal punctuation", "[core][legal-file-name]")
{
    CHECK(legalFileName("Earth, Wind & Fire - September") == "Earth, Wind & Fire - September");
}

// Every character some OS refuses becomes an underscore.
TEST_CASE("legalFileName replaces each illegal character", "[core][legal-file-name]")
{
    CHECK(legalFileName("Why? - Time: 2*2") == "Why_ - Time_ 2_2");
    CHECK(legalFileName("<a> \"b\" \\c|") == "_a_ _b_ _c_");
}

// Control bytes are dropped rather than replaced.
TEST_CASE("legalFileName drops control bytes", "[core][legal-file-name]")
{
    CHECK(legalFileName("Tab\there - Del\x7F") == "Tabhere - Del");
}

// A leading dot would hide the file on macOS and Linux.
TEST_CASE("legalFileName replaces a leading dot", "[core][legal-file-name]")
{
    CHECK(legalFileName(".38 Special - Hold On Loosely") == "_38 Special - Hold On Loosely");
}

// Non-ASCII text is every byte at or above 0x80, so it passes byte for byte.
TEST_CASE("legalFileName keeps non-ASCII text", "[core][legal-file-name]")
{
    const std::string bjork{"Bj\xC3\xB6rk - J\xC3\xB3ga"};
    CHECK(legalFileName(bjork) == bjork);
}

// The cap never splits a multi-byte character.
TEST_CASE("legalFileName caps the stem on a code-point boundary", "[core][legal-file-name]")
{
    // 119 ASCII bytes, then two-byte characters: the 120-byte cap falls inside the first one.
    std::string long_stem(119, 'a');
    while (long_stem.size() < 200)
    {
        long_stem += "\xC3\xB6";
    }

    const std::string legal = legalFileName(long_stem);

    CHECK(legal.size() <= std::size_t{120});
    CHECK(legal == std::string(119, 'a'));
}

} // namespace rock_hero::common::core
