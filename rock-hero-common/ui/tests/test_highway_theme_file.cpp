#include "highway_theme/highway_theme_file.h"

#include <array>
#include <catch2/catch_test_macros.hpp>
#include <expected>
#include <filesystem>
#include <functional>
#include <juce_core/juce_core.h>
#include <juce_graphics/juce_graphics.h>
#include <optional>
#include <rock_hero/common/core/highway/highway_resources.h>
#include <rock_hero/common/core/shared/json.h>
#include <rock_hero/common/core/shared/juce_path.h>
#include <rock_hero/common/ui/highway_theme/highway_theme_registry.h>
#include <rock_hero/common/ui/testing/highway_theme_fixtures.h>
#include <string>
#include <string_view>

namespace rock_hero::common::ui
{

namespace
{

// The default theme's theme.json, parsed afresh on every call: a JUCE object var shares its
// object when copied, so each fixture mutates its own document.
[[nodiscard]] juce::var defaultThemeDocument()
{
    const juce::File file =
        common::core::juceFileFromPath(testing::defaultThemeDirectory() / "theme.json");
    const std::expected<juce::var, common::core::Json::Error> document =
        common::core::Json::parseDocument(file.loadFileAsString());
    REQUIRE(document.has_value());
    return document.value_or(juce::var{});
}

// The JUCE key for one property name.
[[nodiscard]] juce::Identifier keyOf(const std::string_view name)
{
    return juce::Identifier{juce::String{name.data(), name.size()}};
}

// Sets one top-level key of a theme document.
void setKey(const juce::var& document, const std::string_view key, const juce::var& value)
{
    document.getDynamicObject()->setProperty(keyOf(key), value);
}

// Sets one key of a theme document's colors.
void setColor(const juce::var& document, const std::string_view key, const juce::var& value)
{
    document["colors"].getDynamicObject()->setProperty(keyOf(key), value);
}

// Reads one of a theme document's colors as written.
[[nodiscard]] juce::var colorOf(const juce::var& document, const std::string_view key)
{
    return document["colors"][keyOf(key)];
}

// A temporary folder of theme directories, each holding the given theme.json beside a copy of
// the default theme's art; removed with the fixture.
class ThemeFolder
{
public:
    ThemeFolder()
        : m_root{juce::File::getSpecialLocation(juce::File::tempDirectory)
                     .getNonexistentChildFile("rock-hero-theme-test", "", false)}
    {
        REQUIRE(m_root.createDirectory());
    }

    ~ThemeFolder()
    {
        m_root.deleteRecursively();
    }

    ThemeFolder(const ThemeFolder&) = delete;
    ThemeFolder& operator=(const ThemeFolder&) = delete;
    ThemeFolder(ThemeFolder&&) = delete;
    ThemeFolder& operator=(ThemeFolder&&) = delete;

    // The folder itself, to load as a built-in or user themes folder.
    [[nodiscard]] std::filesystem::path path() const
    {
        return common::core::pathFromJuceFile(m_root);
    }

    // The directory a theme with this id has in the folder.
    [[nodiscard]] std::filesystem::path themeAt(const std::string_view id) const
    {
        return path() / id;
    }

    // Writes a theme directory whose theme.json holds the given text.
    void addTheme(const std::string_view id, const juce::String& theme_json) const
    {
        const juce::File directory = common::core::juceFileFromPath(themeAt(id));
        REQUIRE(directory.createDirectory());
        REQUIRE(directory.getChildFile("theme.json").replaceWithText(theme_json));
        const juce::File art = common::core::juceFileFromPath(testing::defaultThemeDirectory());
        for (const common::core::HighwayTexture texture : common::core::g_highway_textures)
        {
            const juce::String file_name{std::string{common::core::highwayTextureFileName(
                texture)}};
            REQUIRE(art.getChildFile(file_name).copyFileTo(directory.getChildFile(file_name)));
        }
    }

    // Writes a theme directory holding the given document.
    void addTheme(const std::string_view id, const juce::var& document) const
    {
        addTheme(id, juce::JSON::toString(document));
    }

private:
    juce::File m_root;
};

// Loads a theme directory and returns why it failed, or nothing when it loaded.
[[nodiscard]] std::optional<HighwayThemeErrorCode> failureOf(const std::filesystem::path& theme)
{
    const std::expected<LoadedHighwayTheme, HighwayThemeError> loaded = loadHighwayTheme(theme);
    if (loaded.has_value())
    {
        return std::nullopt;
    }
    return loaded.error().code;
}

// Writes the default theme's document under an id after one change, and returns why it failed.
[[nodiscard]] std::optional<HighwayThemeErrorCode> failureAfter(
    const std::function<void(const juce::var&)>& change)
{
    const ThemeFolder folder;
    const juce::var document = defaultThemeDocument();
    change(document);
    folder.addTheme("changed", document);
    return failureOf(folder.themeAt("changed"));
}

} // namespace

// Both CSS forms parse in either case; the alpha CSS writes last lands in the packed top byte.
TEST_CASE("CSS hex colors parse in their opaque and alpha forms", "[ui][highway-theme]")
{
    CHECK(parseCssHexColor("#E03CFF") == std::optional<ArgbColor>{0xFFE03CFFU});
    CHECK(parseCssHexColor("#e03cff") == std::optional<ArgbColor>{0xFFE03CFFU});
    CHECK(parseCssHexColor("#5978FF49") == std::optional<ArgbColor>{0x495978FFU});
    CHECK(parseCssHexColor("#5978ff49") == std::optional<ArgbColor>{0x495978FFU});
    CHECK(parseCssHexColor("#00000000") == std::optional<ArgbColor>{0x00000000U});
}

// Anything but exactly '#' and six or eight hex digits is refused, not read in part.
TEST_CASE("CSS hex parsing refuses every other spelling", "[ui][highway-theme]")
{
    for (const std::string_view text :
         {"",
          "#",
          "E03CFF",
          "#E03CF",
          "#E03CFF0",
          "#E03CFF0000",
          "#G03CFF",
          "#-03CFF",
          "#+03CFF",
          " #E03CFF",
          "#E03CFF ",
          "0xE03CFF"})
    {
        INFO(std::string{text});
        CHECK_FALSE(parseCssHexColor(text).has_value());
    }
}

// A complete theme loads with its folder as its id and every value as written.
TEST_CASE("A complete theme directory loads", "[ui][highway-theme]")
{
    const ThemeFolder folder;
    folder.addTheme("night-drive-2", defaultThemeDocument());
    const std::expected<LoadedHighwayTheme, HighwayThemeError> loaded =
        loadHighwayTheme(folder.themeAt("night-drive-2"));
    REQUIRE(loaded.has_value());
    if (!loaded.has_value())
    {
        return;
    }
    CHECK(loaded->id == "night-drive-2");
    CHECK(loaded->name == "Ice");
    CHECK(loaded->colors.backdrop == 0xFF000000U);
    CHECK(loaded->colors.lit_lane == 0x495978FFU);
    CHECK(loaded->colors.inlay_tint == 0xE68691B2U);
    CHECK(loaded->strings.colors.front() == 0xFFE03CFFU);
    CHECK(loaded->strings.colors.back() == 0xFFC1C1C1U);
    for (const common::core::HighwayTexture texture : common::core::g_highway_textures)
    {
        CHECK_FALSE(loaded->textures.at(common::core::indexOf(texture)).empty());
    }
}

// The file's shape is strict: what it cannot parse, a key it does not define (including the head
// shape and inlay choices it has no key for), and inheritance are each refused by name.
TEST_CASE("A theme file outside the format is refused", "[ui][highway-theme]")
{
    {
        const ThemeFolder folder;
        folder.addTheme("broken", juce::String{"{ \"name\": "});
        folder.addTheme("listed", juce::String{"[]"});
        CHECK(failureOf(folder.themeAt("broken")) == HighwayThemeErrorCode::MalformedJson);
        CHECK(failureOf(folder.themeAt("listed")) == HighwayThemeErrorCode::MalformedJson);
    }
    CHECK(failureAfter([](const juce::var& document) {
              setKey(document, "head_shape", "plate");
          }) == HighwayThemeErrorCode::UnknownKey);
    CHECK(failureAfter([](const juce::var& document) {
              setColor(document, "glow", "#FFFFFF");
          }) == HighwayThemeErrorCode::UnknownKey);
    CHECK(failureAfter([](const juce::var& document) {
              setKey(document, "inherits", "ice");
          }) == HighwayThemeErrorCode::InheritanceUnsupported);
    CHECK(failureAfter([](const juce::var& document) {
              setKey(document, "colors", "#FFFFFF");
          }) == HighwayThemeErrorCode::MalformedJson);
}

// Every key the format requires is required: the name, each role and the string colors.
TEST_CASE("A theme file missing a key is refused", "[ui][highway-theme]")
{
    CHECK(failureAfter([](const juce::var& document) {
              document.getDynamicObject()->removeProperty("name");
          }) == HighwayThemeErrorCode::MissingKey);
    CHECK(failureAfter([](const juce::var& document) {
              document["colors"].getDynamicObject()->removeProperty("capo_bar");
          }) == HighwayThemeErrorCode::MissingKey);
    CHECK(failureAfter([](const juce::var& document) {
              document["colors"].getDynamicObject()->removeProperty("strings");
          }) == HighwayThemeErrorCode::MissingKey);
    CHECK(failureAfter([](const juce::var& document) {
              setKey(document, "name", "   ");
          }) == HighwayThemeErrorCode::InvalidName);
}

// A color that is not CSS hex is refused, and so are string colors that are not exactly one
// opaque color per string.
TEST_CASE("A theme file with a bad color is refused", "[ui][highway-theme]")
{
    CHECK(failureAfter([](const juce::var& document) {
              setColor(document, "beat_bar", "#12345");
          }) == HighwayThemeErrorCode::InvalidColor);
    CHECK(failureAfter([](const juce::var& document) {
              setColor(document, "beat_bar", 255);
          }) == HighwayThemeErrorCode::InvalidColor);
    CHECK(failureAfter([](const juce::var& document) {
              colorOf(document, "strings").getArray()->removeLast();
          }) == HighwayThemeErrorCode::InvalidStringColors);
    CHECK(failureAfter([](const juce::var& document) {
              colorOf(document, "strings").getArray()->set(0, "#E03CFF80");
          }) == HighwayThemeErrorCode::InvalidStringColors);
    CHECK(failureAfter([](const juce::var& document) {
              colorOf(document, "strings").getArray()->set(0, "purple");
          }) == HighwayThemeErrorCode::InvalidColor);
}

// The folder name is the id, so a name a settings file could not store is refused.
TEST_CASE("A theme folder whose name is not an id is refused", "[ui][highway-theme]")
{
    const ThemeFolder folder;
    folder.addTheme("Night_Drive", defaultThemeDocument());
    CHECK(failureOf(folder.themeAt("Night_Drive")) == HighwayThemeErrorCode::InvalidFolderName);
}

// The art is held to the texture contract and must all be present.
TEST_CASE("A theme whose art breaks the texture contract is refused", "[ui][highway-theme]")
{
    const ThemeFolder folder;
    folder.addTheme("wrong-inlay", defaultThemeDocument());
    const std::filesystem::path wrong_inlay = folder.themeAt("wrong-inlay");
    const juce::File inlays = common::core::juceFileFromPath(wrong_inlay / "inlays.png");
    REQUIRE(inlays.deleteFile());
    {
        // Scoped so the file is closed before the loader reads it.
        juce::FileOutputStream stream{inlays};
        REQUIRE(stream.openedOk());
        const juce::Image oblong{juce::Image::ARGB, 64, 32, true, juce::SoftwareImageType{}};
        REQUIRE(juce::PNGImageFormat{}.writeImageToStream(oblong, stream));
    }
    CHECK(failureOf(wrong_inlay) == HighwayThemeErrorCode::AssetContractViolated);

    folder.addTheme("no-notes", defaultThemeDocument());
    const std::filesystem::path no_notes = folder.themeAt("no-notes");
    REQUIRE(common::core::juceFileFromPath(no_notes / "notes.png").deleteFile());
    CHECK(failureOf(no_notes) == HighwayThemeErrorCode::MissingFile);
}

// Each distinction the notation draws by color alone is guarded, as are the string colors and the
// lit fret line's lift over the unlit one.
TEST_CASE("A theme that collapses a color distinction is refused", "[ui][highway-theme]")
{
    // Each case copies one color onto another it must stay distinct from.
    constexpr std::array<std::array<std::string_view, 2>, 7> collapses{{
        {"chord_box", "pick_furniture"},
        {"arpeggio", "lane_border"},
        {"fret_number_lit", "fret_number_dim"},
        {"fret_number_lit", "fret_number_target"},
        {"fret_number_dim", "fret_number_target"},
        {"strike_fretting", "strike_picking"},
        {"fret_line_lit", "fret_line"},
    }};
    for (const std::array<std::string_view, 2>& roles : collapses)
    {
        INFO(std::string{roles[0]} + " onto " + std::string{roles[1]});
        CHECK(failureAfter([&roles](const juce::var& document) {
                  setColor(document, roles[1], colorOf(document, roles[0]));
              }) == HighwayThemeErrorCode::GuardrailViolated);
    }

    CHECK(failureAfter([](const juce::var& document) {
              juce::Array<juce::var>& strings = *colorOf(document, "strings").getArray();
              strings.set(1, strings[0]);
          }) == HighwayThemeErrorCode::GuardrailViolated);
}

// Every built-in the products ship loads through the one loader, and the set holds the default
// theme.
TEST_CASE("Every shipped built-in theme loads", "[ui][highway-theme]")
{
    const std::filesystem::path built_in = testing::builtInThemesDirectory();
    int theme_count = 0;
    for (const std::filesystem::directory_entry& entry :
         std::filesystem::directory_iterator{built_in})
    {
        INFO(common::core::utf8FromPath(entry.path()));
        const std::expected<LoadedHighwayTheme, HighwayThemeError> loaded =
            loadHighwayTheme(entry.path());
        if (!loaded.has_value())
        {
            FAIL(loaded.error().message);
        }
        ++theme_count;
    }
    CHECK(theme_count > 0);

    const ThemeFolder no_user_themes;
    const std::expected<HighwayThemeRegistry, HighwayThemeError> registry =
        HighwayThemeRegistry::load(
            HighwayThemeFolders{.built_in = built_in, .user = no_user_themes.path() / "absent"});
    REQUIRE(registry.has_value());
    if (registry.has_value())
    {
        CHECK(registry->defaultTheme().id == g_default_highway_theme_id);
    }
}

// A user folder is untrusted: each theme that fails is dropped alone, a theme reusing a built-in's
// id cannot replace it, and what is left loads normally.
TEST_CASE("A broken user theme is dropped and the registry stays usable", "[ui][highway-theme]")
{
    const ThemeFolder built_in;
    built_in.addTheme(std::string{g_default_highway_theme_id}, defaultThemeDocument());

    const ThemeFolder user;
    const juce::var mine = defaultThemeDocument();
    setKey(mine, "name", "Mine");
    colorOf(mine, "strings").getArray()->set(0, "#FF00FF");
    user.addTheme("mine", mine);
    const juce::var unknown_key = defaultThemeDocument();
    setKey(unknown_key, "glow", true);
    user.addTheme("unknown-key", unknown_key);
    const juce::var imposter = defaultThemeDocument();
    setKey(imposter, "name", "Imposter");
    user.addTheme(std::string{g_default_highway_theme_id}, imposter);
    user.addTheme("Bad Name", defaultThemeDocument());

    const std::expected<HighwayThemeRegistry, HighwayThemeError> registry =
        HighwayThemeRegistry::load(
            HighwayThemeFolders{.built_in = built_in.path(), .user = user.path()});
    REQUIRE(registry.has_value());
    if (!registry.has_value())
    {
        return;
    }
    REQUIRE(registry->themes().size() == 2);
    CHECK(registry->themes()[0].id == g_default_highway_theme_id);
    CHECK(registry->themes()[0].name == "Ice");
    CHECK(registry->themes()[1].id == "mine");
    CHECK(registry->themes()[1].name == "Mine");
    CHECK(registry->defaultTheme().name == "Ice");
}

// Every loaded theme is a palette of its own strings, and the hosts draw with the default
// theme's.
TEST_CASE("Each theme's strings are its palette", "[ui][highway-theme]")
{
    const ThemeFolder built_in;
    built_in.addTheme(std::string{g_default_highway_theme_id}, defaultThemeDocument());
    const juce::var other = defaultThemeDocument();
    colorOf(other, "strings").getArray()->set(0, "#FF00FF");
    built_in.addTheme("other", other);

    const ThemeFolder user;
    const std::expected<HighwayThemeRegistry, HighwayThemeError> registry =
        HighwayThemeRegistry::load(
            HighwayThemeFolders{.built_in = built_in.path(), .user = user.path()});
    REQUIRE(registry.has_value());
    if (!registry.has_value())
    {
        return;
    }
    REQUIRE(registry->themes().size() == 2);
    CHECK(registry->themes()[0].id == g_default_highway_theme_id);
    CHECK(registry->themes()[0].strings.colors.front() == 0xFFE03CFFU);
    CHECK(registry->themes()[1].id == "other");
    CHECK(registry->themes()[1].strings.colors.front() == 0xFFFF00FFU);
    CHECK(registry->defaultTheme().strings.colors.front() == 0xFFE03CFFU);
}

// The player's pick draws when a loaded theme has it; no pick, or a pick no theme has, draws the
// default.
TEST_CASE("A picked theme resolves to itself or to the default", "[ui][highway-theme]")
{
    const ThemeFolder built_in;
    built_in.addTheme(std::string{g_default_highway_theme_id}, defaultThemeDocument());
    const ThemeFolder user;
    const juce::var mine = defaultThemeDocument();
    setKey(mine, "name", "Mine");
    user.addTheme("mine", mine);

    const std::expected<HighwayThemeRegistry, HighwayThemeError> registry =
        HighwayThemeRegistry::load(
            HighwayThemeFolders{.built_in = built_in.path(), .user = user.path()});
    REQUIRE(registry.has_value());
    if (!registry.has_value())
    {
        return;
    }
    CHECK(registry->themeOrDefault(std::string{"mine"}).name == "Mine");
    CHECK(registry->themeOrDefault(std::nullopt).id == g_default_highway_theme_id);
    CHECK(registry->themeOrDefault(std::string{"removed"}).id == g_default_highway_theme_id);
}

// A broken install fails loudly: no built-in folder, no default theme, or any built-in that does
// not load fails the whole registry.
TEST_CASE("A broken built-in set fails the whole registry", "[ui][highway-theme]")
{
    const ThemeFolder user;
    const auto failure = [&user](const std::filesystem::path& built_in) {
        const std::expected<HighwayThemeRegistry, HighwayThemeError> registry =
            HighwayThemeRegistry::load(
                HighwayThemeFolders{.built_in = built_in, .user = user.path()});
        return registry.has_value() ? std::nullopt
                                    : std::optional<HighwayThemeErrorCode>{registry.error().code};
    };

    CHECK(failure(user.path() / "absent") == HighwayThemeErrorCode::MissingThemesFolder);

    const ThemeFolder without_default;
    without_default.addTheme("other", defaultThemeDocument());
    CHECK(failure(without_default.path()) == HighwayThemeErrorCode::MissingDefaultTheme);

    const ThemeFolder with_broken;
    with_broken.addTheme(std::string{g_default_highway_theme_id}, defaultThemeDocument());
    const juce::var broken = defaultThemeDocument();
    setColor(broken, "beat_bar", "#nothex");
    with_broken.addTheme("broken", broken);
    CHECK(failure(with_broken.path()) == HighwayThemeErrorCode::InvalidColor);
}

} // namespace rock_hero::common::ui
