#include "highway_theme/highway_theme_file.h"
#include "highway_theme/highway_theme_guardrails.h"

#include <array>
#include <catch2/catch_test_macros.hpp>
#include <expected>
#include <filesystem>
#include <functional>
#include <juce_core/juce_core.h>
#include <juce_graphics/juce_graphics.h>
#include <optional>
#include <rock_hero/common/core/highway/highway_player_settings.h>
#include <rock_hero/common/core/highway/highway_resources.h>
#include <rock_hero/common/core/highway/highway_view_preferences.h>
#include <rock_hero/common/core/shared/json.h>
#include <rock_hero/common/core/shared/juce_path.h>
#include <rock_hero/common/ui/highway_theme/highway_theme_registry.h>
#include <rock_hero/common/ui/highway_theme/loaded_highway_theme.h>
#include <rock_hero/common/ui/testing/highway_theme_fixtures.h>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

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

// A span role's value split by surface.
[[nodiscard]] juce::var splitColor(const juce::var& highway, const juce::var& tab)
{
    const juce::DynamicObject::Ptr halves{new juce::DynamicObject{}};
    halves->setProperty("highway", highway);
    halves->setProperty("tab", tab);
    return juce::var{halves.get()};
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

    // Writes a theme directory holding only a theme.json with the given document, as a theme that
    // inherits its art does.
    void addThemeFileOnly(const std::string_view id, const juce::var& document) const
    {
        writeThemeJson(id, juce::JSON::toString(document));
    }

    // Writes a theme directory whose theme.json holds the given text, beside a copy of the default
    // theme's art.
    void addTheme(const std::string_view id, const juce::String& theme_json) const
    {
        writeThemeJson(id, theme_json);
        const juce::File directory = common::core::juceFileFromPath(themeAt(id));
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
    // Writes a theme directory holding only a theme.json with the given text.
    void writeThemeJson(const std::string_view id, const juce::String& theme_json) const
    {
        const juce::File directory = common::core::juceFileFromPath(themeAt(id));
        REQUIRE(directory.createDirectory());
        REQUIRE(directory.getChildFile("theme.json").replaceWithText(theme_json));
    }

    juce::File m_root;
};

// A theme document that inherits the default theme and states only its name.
[[nodiscard]] juce::var inheritingDocument(const std::string_view name)
{
    const juce::DynamicObject::Ptr document{new juce::DynamicObject{}};
    document->setProperty("name", juce::String{std::string{name}});
    document->setProperty("inherits", juce::String{std::string{g_default_highway_theme_id}});
    return juce::var{document.get()};
}

// Sets one color of a document that may not hold `colors` yet.
void setOwnColor(const juce::var& document, const std::string_view key, const juce::var& value)
{
    if (!document["colors"].isObject())
    {
        const juce::DynamicObject::Ptr colors{new juce::DynamicObject{}};
        setKey(document, "colors", juce::var{colors.get()});
    }
    setColor(document, key, value);
}

// Loads the registry over a built-in folder and a user folder; a failure throws its message, which
// fails the test.
[[nodiscard]] HighwayThemeRegistry registryOver(
    const std::filesystem::path& built_in, const std::filesystem::path& user)
{
    std::expected<HighwayThemeRegistry, HighwayThemeError> registry =
        HighwayThemeRegistry::load(HighwayThemeFolders{.built_in = built_in, .user = user});
    if (!registry.has_value())
    {
        throw std::runtime_error{registry.error().message};
    }
    return std::move(*registry);
}

// The id of a palette, or empty for "Theme's choice".
[[nodiscard]] std::string pinnedIdOf(const HighwayThemePalette* const palette)
{
    return palette != nullptr ? palette->source_id : std::string{};
}

// Loads a theme directory with no bases and returns why it failed, or nothing when it loaded.
[[nodiscard]] std::optional<HighwayThemeErrorCode> failureOf(const std::filesystem::path& theme)
{
    const std::expected<LoadedHighwayTheme, HighwayThemeError> loaded = loadHighwayTheme(theme, {});
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
        loadHighwayTheme(folder.themeAt("night-drive-2"), {});
    REQUIRE(loaded.has_value());
    if (!loaded.has_value())
    {
        return;
    }
    CHECK(loaded->id == "night-drive-2");
    CHECK(loaded->name == "Default");
    CHECK(loaded->colors.backdrop == 0xFF000000U);
    CHECK(loaded->colors.lit_lane == 0x2F5978FFU);
    CHECK(loaded->colors.inlay_tint == 0xE68691B2U);
    CHECK(loaded->colors.chord == 0xFF5CD3F5U);
    CHECK(loaded->tab_inks.chord == 0xFF4173DEU);
    CHECK(loaded->tab_inks.arpeggio == 0xFF9061C7U);
    CHECK(loaded->palette.source_id == "night-drive-2");
    CHECK(loaded->palette.name == "Default");
    CHECK(loaded->palette.colors.colors.front() == 0xFFE03CFFU);
    CHECK(loaded->palette.colors.colors.back() == 0xFFC1C1C1U);
    for (const common::core::HighwayTexture texture : common::core::g_highway_textures)
    {
        CHECK_FALSE(loaded->textures.at(common::core::indexOf(texture)).empty());
    }
}

// The file's shape is strict: what it cannot parse, a key it does not define (including the head
// shape and inlay choices it has no key for), and a base it may not inherit are each refused by
// name. A theme loaded with no bases, as the default theme is, may inherit nothing.
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
              setKey(document, "inherits", "default");
          }) == HighwayThemeErrorCode::UnknownBase);
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
    // Each case copies one color onto another it must stay distinct from; a color copied onto
    // lane_line_lit reaches the dim fret number derived from it.
    constexpr std::array<std::array<std::string_view, 2>, 6> collapses{{
        {"chord", "arpeggio"},
        {"fret_number_lit", "lane_line_lit"},
        {"fret_number_lit", "fret_number_target"},
        {"fret_number_target", "lane_line_lit"},
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

// The hand and the kind of span are both read by color, the picking hand's derived from the
// fretting hand's: each crafted pair of chord and arpeggio fails exactly the distinction named.
TEST_CASE("A theme whose hands or kinds of span collapse is refused", "[ui][highway-theme]")
{
    const StringColorPalette strings = testing::fixtureStringColors();
    const auto failure = [&strings](const ArgbColor chord, const ArgbColor arpeggio) {
        HighwayTheme colors = testing::fixtureHighwayTheme();
        colors.chord = chord;
        colors.arpeggio = arpeggio;
        const std::expected<void, HighwayThemeError> checked =
            checkHighwayThemeGuardrails(colors, testing::fixtureTabShapeInks(), strings);
        return checked.has_value() ? std::string{} : checked.error().message;
    };

    CHECK(failure(0xFF00D0D0U, 0xFFC040FFU).empty());
    // The two kinds alike.
    CHECK(failure(0xFF00D0D0U, 0xFF00D0D0U).starts_with("chord #00D0D0 and arpeggio #00D0D0 "));
    // A grey has no colorfulness for its picking-hand counterpart to give up.
    CHECK(failure(0xFF808080U, 0xFFC040FFU)
              .starts_with("chord #808080 and the picking hand's chord #808080 "));
    CHECK(failure(0xFF00D0D0U, 0xFF808080U)
              .starts_with("arpeggio #808080 and the picking hand's arpeggio #808080 "));
    // Two kinds far enough apart on the fretting hand fall too close once each keeps a quarter of
    // its chroma.
    CHECK(failure(0xFF00D0D0U, 0xFF60D0A0U)
              .starts_with("the picking hand's chord #AFCECD and the picking hand's arpeggio "));
}

// The tab lane's two inks are held apart like the highway's, and each dark enough for the white
// chip label: L* 60 at most, a grey either side of it deciding the edge.
TEST_CASE("The tab lane's span inks stay apart and carry a white label", "[ui][highway-theme]")
{
    const auto failure = [](const ArgbColor chord, const ArgbColor arpeggio) {
        const std::expected<void, HighwayThemeError> checked = checkHighwayThemeGuardrails(
            testing::fixtureHighwayTheme(),
            TabShapeInks{.chord = chord, .arpeggio = arpeggio},
            testing::fixtureStringColors());
        return checked.has_value() ? std::string{} : checked.error().message;
    };

    CHECK(failure(0xFF2E6BE8U, 0xFF9061C7U).empty());
    CHECK(failure(0xFF2E6BE8U, 0xFF2E6BE8U)
              .starts_with("the tab lane's chord #2E6BE8 and the tab lane's arpeggio #2E6BE8 "));
    CHECK(failure(0xFF5CD3F5U, 0xFF9061C7U).starts_with("the tab lane's chord #5CD3F5 is L* 79.3"));
    CHECK(failure(0xFF2E6BE8U, 0xFFFFB800U).starts_with("the tab lane's arpeggio #FFB800 is L* "));
    CHECK(failure(0xFF909090U, 0xFF9061C7U).empty());
    CHECK(failure(0xFF919191U, 0xFF9061C7U).starts_with("the tab lane's chord #919191 is L* 60.2"));
}

// A span role is one color for both surfaces, or a pair split by surface; any other role is one
// color.
TEST_CASE("A span role is one color or a pair split by surface", "[ui][highway-theme]")
{
    const ThemeFolder folder;
    const juce::var shared = defaultThemeDocument();
    setColor(shared, "chord", "#C81E1E");
    folder.addTheme("shared", shared);
    const std::expected<LoadedHighwayTheme, HighwayThemeError> loaded =
        loadHighwayTheme(folder.themeAt("shared"), {});
    REQUIRE(loaded.has_value());
    if (!loaded.has_value())
    {
        return;
    }
    CHECK(loaded->colors.chord == 0xFFC81E1EU);
    CHECK(loaded->tab_inks.chord == 0xFFC81E1EU);
    CHECK(loaded->colors.arpeggio == 0xFFA361FFU);
    CHECK(loaded->tab_inks.arpeggio == 0xFF9061C7U);

    CHECK(failureAfter([](const juce::var& document) {
              const juce::DynamicObject::Ptr half{new juce::DynamicObject{}};
              half->setProperty("highway", "#5CD3F5");
              setColor(document, "chord", juce::var{half.get()});
          }) == HighwayThemeErrorCode::MissingKey);
    CHECK(failureAfter([](const juce::var& document) {
              const juce::var halves = splitColor("#5CD3F5", "#4173DE");
              halves.getDynamicObject()->setProperty("game", "#4173DE");
              setColor(document, "chord", halves);
          }) == HighwayThemeErrorCode::UnknownKey);
    CHECK(failureAfter([](const juce::var& document) {
              setColor(document, "chord", splitColor("#5CD3F5", "blue"));
          }) == HighwayThemeErrorCode::InvalidColor);
    CHECK(failureAfter([](const juce::var& document) {
              setColor(document, "beat_bar", splitColor("#243480", "#243480"));
          }) == HighwayThemeErrorCode::InvalidColor);
    // A chip's label prints on a tab ink, so the ink is opaque, in either spelling.
    CHECK(failureAfter([](const juce::var& document) {
              setColor(document, "chord", splitColor("#5CD3F5", "#4173DE80"));
          }) == HighwayThemeErrorCode::InvalidColor);
    CHECK(failureAfter([](const juce::var& document) {
              setColor(document, "chord", "#4173DE80");
          }) == HighwayThemeErrorCode::InvalidColor);
    // The tab halves are guarded: a light one, and one collapsed onto the other kind's.
    CHECK(failureAfter([](const juce::var& document) {
              setColor(document, "chord", splitColor("#5CD3F5", "#5CD3F5"));
          }) == HighwayThemeErrorCode::GuardrailViolated);
    CHECK(failureAfter([](const juce::var& document) {
              setColor(document, "chord", splitColor("#5CD3F5", "#9061C7"));
          }) == HighwayThemeErrorCode::GuardrailViolated);
}

// An inheriting theme states a span role whole or not at all: one color replaces the base's pair
// on both surfaces, so the lane never keeps the base's ink under the theme's own boxes.
TEST_CASE("An inheriting theme states a span role whole", "[ui][highway-theme]")
{
    const std::expected<LoadedHighwayTheme, HighwayThemeError> default_theme =
        loadHighwayTheme(testing::defaultThemeDirectory(), {});
    REQUIRE(default_theme.has_value());
    if (!default_theme.has_value())
    {
        return;
    }
    const std::span<const LoadedHighwayTheme> bases{&*default_theme, 1};
    const auto load = [bases](const juce::var& document) {
        const ThemeFolder folder;
        folder.addThemeFileOnly("inheriting", document);
        return loadHighwayTheme(folder.themeAt("inheriting"), bases);
    };

    const std::expected<LoadedHighwayTheme, HighwayThemeError> plain =
        load(inheritingDocument("Plain"));
    const juce::var red = inheritingDocument("Red");
    setOwnColor(red, "chord", "#C81E1E");
    const std::expected<LoadedHighwayTheme, HighwayThemeError> red_chord = load(red);
    REQUIRE(plain.has_value());
    REQUIRE(red_chord.has_value());
    if (!plain.has_value() || !red_chord.has_value())
    {
        return;
    }
    CHECK(plain->tab_inks.chord == default_theme->tab_inks.chord);
    CHECK(plain->tab_inks.arpeggio == default_theme->tab_inks.arpeggio);
    CHECK(red_chord->colors.chord == 0xFFC81E1EU);
    CHECK(red_chord->tab_inks.chord == 0xFFC81E1EU);
    CHECK(red_chord->tab_inks.arpeggio == default_theme->tab_inks.arpeggio);
}

// The CRT look is one switch that inherits like every other key: a theme leaving it out takes its
// base's, so a theme built on a CRT theme keeps the look unless it states false.
TEST_CASE("A theme's CRT look is its own or its base's", "[ui][highway-theme]")
{
    const std::expected<LoadedHighwayTheme, HighwayThemeError> default_theme =
        loadHighwayTheme(testing::defaultThemeDirectory(), {});
    REQUIRE(default_theme.has_value());
    if (!default_theme.has_value())
    {
        return;
    }
    CHECK_FALSE(default_theme->crt);

    const ThemeFolder folder;
    const juce::var arcade_document = inheritingDocument("Arcade");
    setKey(arcade_document, "crt", true);
    folder.addThemeFileOnly("arcade", arcade_document);
    const std::expected<LoadedHighwayTheme, HighwayThemeError> arcade =
        loadHighwayTheme(folder.themeAt("arcade"), std::span{&*default_theme, 1});
    REQUIRE(arcade.has_value());
    if (!arcade.has_value())
    {
        return;
    }
    CHECK(arcade->crt);

    const std::array bases{*default_theme, *arcade};
    const auto load = [&folder, &bases](const std::string& id, const juce::var& document) {
        folder.addThemeFileOnly(id, document);
        return loadHighwayTheme(folder.themeAt(id), bases);
    };
    const juce::var child_document = inheritingDocument("Child");
    setKey(child_document, "inherits", "arcade");
    const juce::var colors_only_document = inheritingDocument("Colors Only");
    setKey(colors_only_document, "inherits", "arcade");
    setKey(colors_only_document, "crt", false);
    const std::expected<LoadedHighwayTheme, HighwayThemeError> child =
        load("child", child_document);
    const std::expected<LoadedHighwayTheme, HighwayThemeError> colors_only =
        load("colors-only", colors_only_document);
    REQUIRE(child.has_value());
    REQUIRE(colors_only.has_value());
    if (!child.has_value() || !colors_only.has_value())
    {
        return;
    }
    CHECK(child->crt);
    CHECK_FALSE(colors_only->crt);

    CHECK(failureAfter([](const juce::var& document) {
              setKey(document, "crt", "yes");
          }) == HighwayThemeErrorCode::MalformedJson);
}

// Every built-in the products ship loads through the registry, which fails on the first that does
// not (naming it), so one theme per shipped directory comes back, the default among them.
TEST_CASE("Every shipped built-in theme loads", "[ui][highway-theme]")
{
    const std::filesystem::path built_in = testing::builtInThemesDirectory();
    const ThemeFolder no_user_themes;
    const HighwayThemeRegistry registry = registryOver(built_in, no_user_themes.path() / "absent");

    const auto directory_count = std::ranges::count_if(
        std::filesystem::directory_iterator{built_in},
        [](const std::filesystem::directory_entry& entry) { return entry.is_directory(); });
    CHECK(std::cmp_equal(registry.themes().size(), directory_count));
    CHECK(registry.themes().size() > 1);
    CHECK(
        registry.themeFor(common::core::HighwayPlayerSettings{}).id == g_default_highway_theme_id);

    // Retro Arcade is the one built-in presented through the CRT look.
    for (const LoadedHighwayTheme& theme : registry.themes())
    {
        CHECK(theme.crt == (theme.id == "retro-arcade"));
    }
}

// A user folder is untrusted: each theme that fails is dropped alone, including one inheriting a
// theme that is not a built-in, and what is left loads normally.
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
    user.addTheme("Bad Name", defaultThemeDocument());
    const juce::var unknown_base = inheritingDocument("Unknown Base");
    setKey(unknown_base, "inherits", "nowhere");
    user.addThemeFileOnly("unknown-base", unknown_base);
    const juce::var user_base = inheritingDocument("User Base");
    setKey(user_base, "inherits", "mine");
    user.addThemeFileOnly("user-base", user_base);

    const HighwayThemeRegistry registry = registryOver(built_in.path(), user.path());
    REQUIRE(registry.themes().size() == 2);
    CHECK(registry.themes()[0].id == g_default_highway_theme_id);
    CHECK(registry.themes()[0].name == "Default");
    CHECK(registry.themes()[1].id == "mine");
    CHECK(registry.themes()[1].name == "Mine");
    CHECK(failureOf(user.themeAt("unknown-base")) == HighwayThemeErrorCode::UnknownBase);
}

// An inheriting theme takes each role it leaves out from its base one by one, and its strings and
// art whole, so it is no palette of its own; its name is its own.
TEST_CASE("An inheriting theme takes what it leaves out from its base", "[ui][highway-theme]")
{
    const ThemeFolder built_in;
    built_in.addTheme(std::string{g_default_highway_theme_id}, defaultThemeDocument());
    const ThemeFolder user;
    const juce::var tweak = inheritingDocument("Tweak");
    setOwnColor(tweak, "beat_bar", "#102030");
    user.addThemeFileOnly("tweak", tweak);

    const HighwayThemeRegistry registry = registryOver(built_in.path(), user.path());
    REQUIRE(registry.themes().size() == 2);
    const LoadedHighwayTheme& base = registry.themes()[0];
    const LoadedHighwayTheme& inheriting = registry.themes()[1];
    CHECK(inheriting.id == "tweak");
    CHECK(inheriting.name == "Tweak");
    CHECK(inheriting.colors.beat_bar == 0xFF102030U);
    CHECK(inheriting.colors.lit_lane == base.colors.lit_lane);
    CHECK(inheriting.colors.label_text == base.colors.label_text);
    CHECK(inheriting.palette.source_id == g_default_highway_theme_id);
    CHECK(inheriting.palette.name == "Default");
    CHECK(inheriting.palette.colors.colors == base.palette.colors.colors);
    CHECK(inheriting.textures == base.textures);
    REQUIRE(registry.palettes().size() == 1);
    CHECK(registry.palettes().front().get().source_id == g_default_highway_theme_id);
}

// A theme that states only its strings registers a palette named by itself, beside its base's.
TEST_CASE("A strings-only theme is a palette of its own", "[ui][highway-theme]")
{
    const ThemeFolder built_in;
    built_in.addTheme(std::string{g_default_highway_theme_id}, defaultThemeDocument());
    const ThemeFolder user;
    const juce::var my_strings = inheritingDocument("My Strings");
    setOwnColor(my_strings, "strings", colorOf(defaultThemeDocument(), "strings"));
    colorOf(my_strings, "strings").getArray()->set(0, "#FF00FF");
    user.addThemeFileOnly("my-strings", my_strings);

    const HighwayThemeRegistry registry = registryOver(built_in.path(), user.path());
    REQUIRE(registry.themes().size() == 2);
    const LoadedHighwayTheme& own = registry.themes()[1];
    CHECK(own.colors.beat_bar == registry.themes()[0].colors.beat_bar);
    CHECK(own.palette.source_id == "my-strings");
    CHECK(own.palette.name == "My Strings");
    CHECK(own.palette.colors.colors.front() == 0xFFFF00FFU);
    const std::vector<std::reference_wrapper<const HighwayThemePalette>> palettes =
        registry.palettes();
    REQUIRE(palettes.size() == 2);
    CHECK(palettes[0].get().source_id == g_default_highway_theme_id);
    CHECK(palettes[1].get().source_id == "my-strings");
}

// What an inheriting theme does state is held to the same rules, and the guardrails and the
// texture contract judge the theme it resolves to.
TEST_CASE("An inheriting theme stays strict", "[ui][highway-theme]")
{
    const std::expected<LoadedHighwayTheme, HighwayThemeError> default_theme =
        loadHighwayTheme(testing::defaultThemeDirectory(), {});
    REQUIRE(default_theme.has_value());
    if (!default_theme.has_value())
    {
        return;
    }
    const std::span<const LoadedHighwayTheme> bases{&*default_theme, 1};
    const auto failure = [bases](const juce::var& document) {
        const ThemeFolder folder;
        folder.addThemeFileOnly("inheriting", document);
        const std::expected<LoadedHighwayTheme, HighwayThemeError> loaded =
            loadHighwayTheme(folder.themeAt("inheriting"), bases);
        return loaded.has_value() ? std::nullopt
                                  : std::optional<HighwayThemeErrorCode>{loaded.error().code};
    };

    CHECK_FALSE(failure(inheritingDocument("Plain")).has_value());
    const juce::var unknown_role = inheritingDocument("Unknown Role");
    setOwnColor(unknown_role, "glow", "#FFFFFF");
    CHECK(failure(unknown_role) == HighwayThemeErrorCode::UnknownKey);
    const juce::var nameless = inheritingDocument("Nameless");
    nameless.getDynamicObject()->removeProperty("name");
    CHECK(failure(nameless) == HighwayThemeErrorCode::MissingKey);
    const juce::var collapsed = inheritingDocument("Collapsed");
    setOwnColor(collapsed, "arpeggio", colorOf(defaultThemeDocument(), "chord"));
    CHECK(failure(collapsed) == HighwayThemeErrorCode::GuardrailViolated);

    // Art the theme does carry must load and keep the contract, whatever its base holds.
    const ThemeFolder folder;
    folder.addThemeFileOnly("empty-art", inheritingDocument("Empty Art"));
    REQUIRE(
        common::core::juceFileFromPath(folder.themeAt("empty-art") / "notes.png")
            .replaceWithText(""));
    const std::expected<LoadedHighwayTheme, HighwayThemeError> empty_art =
        loadHighwayTheme(folder.themeAt("empty-art"), bases);
    CHECK((!empty_art.has_value() && empty_art.error().code == HighwayThemeErrorCode::MissingFile));
}

// A user theme with a built-in's id takes its place, so the default resolves to it; it still
// inherits the built-in itself.
TEST_CASE("A user theme with a built-in's id shadows the built-in", "[ui][highway-theme]")
{
    const ThemeFolder built_in;
    built_in.addTheme(std::string{g_default_highway_theme_id}, defaultThemeDocument());
    const ThemeFolder user;
    const juce::var shadow = inheritingDocument("Default Tweaked");
    setOwnColor(shadow, "beat_bar", "#102030");
    user.addThemeFileOnly(g_default_highway_theme_id, shadow);

    const HighwayThemeRegistry registry = registryOver(built_in.path(), user.path());
    REQUIRE(registry.themes().size() == 1);
    const LoadedHighwayTheme& drawn = registry.themeFor(common::core::HighwayPlayerSettings{});
    CHECK(&drawn == registry.themes().data());
    CHECK(drawn.name == "Default Tweaked");
    CHECK(drawn.colors.beat_bar == 0xFF102030U);
    CHECK(drawn.palette.name == "Default");
    REQUIRE(registry.palettes().size() == 1);
    CHECK(&registry.palettes().front().get() == &drawn.palette);
    // Its inherited strings still list, and pin, under the built-in's id and name.
    const common::core::HighwayPlayerSettings pinned{
        .theme_id = std::nullopt,
        .pinned_palette_id = std::string{g_default_highway_theme_id},
        .view = common::core::HighwayViewPreferences{},
    };
    CHECK(registry.pinnedPaletteFor(pinned) == &drawn.palette);
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

    const HighwayThemeRegistry registry = registryOver(built_in.path(), user.path());
    const auto theme_named = [&registry](std::optional<std::string> theme_id) {
        return registry
            .themeFor(
                common::core::HighwayPlayerSettings{
                    .theme_id = std::move(theme_id),
                    .pinned_palette_id = std::nullopt,
                    .view = common::core::HighwayViewPreferences{},
                })
            .name;
    };
    CHECK(theme_named("mine") == "Mine");
    CHECK(theme_named(std::nullopt) == "Default");
    CHECK(theme_named("removed") == "Default");
}

// Unpinned, the palette is the drawn theme's choice and moves with a theme switch; pinned, it stays
// across every theme. A pin naming no palette, whether its theme is gone or now inherits its
// strings, is the theme's choice.
TEST_CASE("A palette resolves to its pin or to the theme's choice", "[ui][highway-theme]")
{
    const ThemeFolder built_in;
    built_in.addTheme(std::string{g_default_highway_theme_id}, defaultThemeDocument());
    const juce::var other = defaultThemeDocument();
    setKey(other, "name", "Other");
    colorOf(other, "strings").getArray()->set(0, "#FF00FF");
    built_in.addTheme("other", other);
    const ThemeFolder user;
    user.addThemeFileOnly("tweak", inheritingDocument("Tweak"));

    const HighwayThemeRegistry registry = registryOver(built_in.path(), user.path());
    const auto resolved = [&registry](const char* theme_id, std::optional<std::string> pin) {
        const common::core::HighwayPlayerSettings settings{
            .theme_id = std::string{theme_id},
            .pinned_palette_id = std::move(pin),
            .view = common::core::HighwayViewPreferences{},
        };
        return std::pair{
            registry.paletteFor(settings).source_id,
            pinnedIdOf(registry.pinnedPaletteFor(settings)),
        };
    };

    // Theme's choice follows the theme, through an inheriting theme to the base that wrote it.
    CHECK(resolved("default", std::nullopt) == std::pair{std::string{"default"}, std::string{}});
    CHECK(resolved("other", std::nullopt) == std::pair{std::string{"other"}, std::string{}});
    CHECK(resolved("tweak", std::nullopt) == std::pair{std::string{"default"}, std::string{}});

    // A pin draws over every theme.
    CHECK(resolved("default", "other") == std::pair{std::string{"other"}, std::string{"other"}});
    CHECK(resolved("tweak", "other") == std::pair{std::string{"other"}, std::string{"other"}});

    // A pin that names no palette falls back to the theme's choice.
    CHECK(resolved("other", "removed") == std::pair{std::string{"other"}, std::string{}});
    CHECK(resolved("other", "tweak") == std::pair{std::string{"other"}, std::string{}});
}

// A broken install fails loudly: no built-in folder, no default theme, or any built-in that does
// not load, one inheriting a theme that is not the default included, fails the whole registry.
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

    // A built-in may inherit the default theme and nothing else: not another built-in, whatever
    // the folder order.
    const ThemeFolder with_inheriting;
    with_inheriting.addTheme(std::string{g_default_highway_theme_id}, defaultThemeDocument());
    with_inheriting.addThemeFileOnly("accent", inheritingDocument("Accent"));
    CHECK_FALSE(failure(with_inheriting.path()).has_value());
    const juce::var chained = inheritingDocument("Chained");
    setKey(chained, "inherits", "accent");
    with_inheriting.addThemeFileOnly("chained", chained);
    CHECK(failure(with_inheriting.path()) == HighwayThemeErrorCode::UnknownBase);
}

} // namespace rock_hero::common::ui
