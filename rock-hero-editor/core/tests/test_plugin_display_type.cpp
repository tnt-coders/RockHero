#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <optional>
#include <rock_hero/editor/core/plugin_display_type.h>
#include <utility>
#include <vector>

namespace rock_hero::editor::core
{

namespace
{

[[nodiscard]] std::filesystem::path testPluginDisplayTypeOverridesFile()
{
    return std::filesystem::path{TEST_PLUGIN_DISPLAY_TYPE_OVERRIDES_FILE};
}

[[nodiscard]] PluginDisplayTypeOverrides readTestDisplayTypeOverrides()
{
    auto overrides = readPluginDisplayTypeOverridesFile(testPluginDisplayTypeOverridesFile());
    REQUIRE(overrides.has_value());
    return std::move(*overrides);
}

} // namespace

// Verifies common VST3 categories map to stable editor display types.
TEST_CASE("PluginDisplayType classifies scanner categories", "[core][plugin-display-type]")
{
    CHECK(
        classifyPluginDisplay(PluginDisplayMetadata{.category = "Fx|Delay"}).primary_type ==
        PluginDisplayType::Delay);
    CHECK(
        classifyPluginDisplay(PluginDisplayMetadata{.category = "Fx|Distortion"}).primary_type ==
        PluginDisplayType::Distortion);
    CHECK(
        classifyPluginDisplay(PluginDisplayMetadata{.category = "Fx|Reverb"}).primary_type ==
        PluginDisplayType::Reverb);
    CHECK(
        classifyPluginDisplay(PluginDisplayMetadata{.category = "Instrument|Synth"}).primary_type ==
        PluginDisplayType::Instrument);
}

// Verifies routing and container tokens do not become user-facing plugin types.
TEST_CASE("PluginDisplayType ignores generic category tokens", "[core][plugin-display-type]")
{
    const PluginDisplayClassification reverb =
        classifyPluginDisplay(PluginDisplayMetadata{.category = "Fx|Reverb|Stereo"});
    CHECK(reverb.primary_type == PluginDisplayType::Reverb);
    CHECK(reverb.scanned_types == std::vector{PluginDisplayType::Reverb});
    CHECK(reverb.filter_types == std::vector{PluginDisplayType::Reverb});

    const PluginDisplayClassification generic =
        classifyPluginDisplay(PluginDisplayMetadata{.category = "Fx|Stereo"});
    CHECK(generic.primary_type == PluginDisplayType::Uncategorized);
    CHECK(generic.scanned_types.empty());
    CHECK(generic.filter_types == std::vector{PluginDisplayType::Uncategorized});
}

// Verifies the shipped default override file is valid and contains curated plugin rows.
TEST_CASE("PluginDisplayType reads default override config", "[core][plugin-display-type]")
{
    const PluginDisplayTypeOverrides overrides = readTestDisplayTypeOverrides();

    const auto neural_amp_modeler = std::ranges::find(
        overrides,
        PluginDisplayTypeOverride{
            .name = "Neural Amp Modeler",
            .display_type = PluginDisplayType::Amp,
        });
    CHECK(neural_amp_modeler != overrides.end());

    const auto ignite_nadir = std::ranges::find(
        overrides,
        PluginDisplayTypeOverride{
            .name = "Ignite - NadIR",
            .display_type = PluginDisplayType::Cab,
        });
    CHECK(ignite_nadir != overrides.end());
}

// Verifies malformed override config fails before it can affect classification.
TEST_CASE("PluginDisplayType rejects invalid override config", "[core][plugin-display-type]")
{
    const auto result = readPluginDisplayTypeOverrides(
        R"json({"version":1,"overrides":[{"name":"Bad","type":"made-up"}]})json");

    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().code == PluginDisplayTypeConfigErrorCode::UnknownType);
}

// Verifies the override config keeps one row for each normalized plugin name.
TEST_CASE("PluginDisplayType rejects duplicate override names", "[core][plugin-display-type]")
{
    const auto result = readPluginDisplayTypeOverrides(
        R"json({"version":1,"overrides":[{"name":"Ignite - NadIR","type":"cab"},{"name":"Ignite NadIR","type":"amp"}]})json");

    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().code == PluginDisplayTypeConfigErrorCode::InvalidSchema);
}

// Verifies free-text plugin names are not guessed when scanner categories are missing.
TEST_CASE("PluginDisplayType does not guess from names", "[core][plugin-display-type]")
{
    CHECK(
        classifyPluginDisplay(
            PluginDisplayMetadata{
                .name = "Definitely Delay",
                .manufacturer = "Amp Words Audio",
                .format_name = "VST3",
            })
            .primary_type == PluginDisplayType::Uncategorized);
}

// Verifies exact plugin overrides select the primary type without hiding scanner-derived filters.
TEST_CASE("PluginDisplayType applies exact display overrides", "[core][plugin-display-type]")
{
    const PluginDisplayTypeOverrides overrides = readTestDisplayTypeOverrides();

    const PluginDisplayClassification classification = classifyPluginDisplay(
        PluginDisplayMetadata{
            .name = "Archetype Nolly X",
            .manufacturer = "Neural DSP",
            .format_name = "VST3",
            .category = "Fx|Distortion|Dynamics|Reverb",
        },
        overrides);

    CHECK(classification.primary_type == PluginDisplayType::Amp);
    CHECK(
        classification.scanned_types == std::vector{
                                            PluginDisplayType::Distortion,
                                            PluginDisplayType::Dynamics,
                                            PluginDisplayType::Reverb,
                                        });
    CHECK(
        classification.filter_types == std::vector{
                                           PluginDisplayType::Amp,
                                           PluginDisplayType::Distortion,
                                           PluginDisplayType::Dynamics,
                                           PluginDisplayType::Reverb,
                                       });

    const PluginDisplayClassification parametric_od = classifyPluginDisplay(
        PluginDisplayMetadata{
            .name = "ParametricOD",
            .manufacturer = "Steven Atkinson",
            .format_name = "VST3",
            .category = "Fx",
        },
        overrides);
    CHECK(parametric_od.primary_type == PluginDisplayType::Distortion);
    CHECK(parametric_od.scanned_types.empty());
    CHECK(parametric_od.filter_types == std::vector{PluginDisplayType::Distortion});

    const PluginDisplayClassification ignite_emissary = classifyPluginDisplay(
        PluginDisplayMetadata{
            .name = "Ignite - Emissary",
            .manufacturer = "Ignite Amps",
            .format_name = "VST3",
            .category = "Fx",
        },
        overrides);
    CHECK(ignite_emissary.primary_type == PluginDisplayType::Amp);
    CHECK(ignite_emissary.scanned_types.empty());
    CHECK(ignite_emissary.filter_types == std::vector{PluginDisplayType::Amp});

    const PluginDisplayClassification ignite_nadir = classifyPluginDisplay(
        PluginDisplayMetadata{
            .name = "Ignite - NadIR",
            .manufacturer = "Unexpected Ignite Metadata",
            .format_name = "VST3",
            .category = "Fx",
        },
        overrides);
    CHECK(ignite_nadir.primary_type == PluginDisplayType::Cab);
    CHECK(ignite_nadir.scanned_types.empty());
    CHECK(ignite_nadir.filter_types == std::vector{PluginDisplayType::Cab});
}

// Verifies persisted display type tokens are stable and invalid tokens are rejected.
TEST_CASE("PluginDisplayType parses stable tokens", "[core][plugin-display-type]")
{
    CHECK(pluginDisplayTypeToken(PluginDisplayType::Cab) == "cab");
    CHECK(pluginDisplayTypeFromToken("cab") == std::optional{PluginDisplayType::Cab});
    CHECK(
        pluginDisplayTypeFromToken("  Distortion ") ==
        std::optional{PluginDisplayType::Distortion});
    CHECK_FALSE(pluginDisplayTypeFromToken("made-up").has_value());
}

// Verifies display labels stay centralized for browser filters and UI rows.
TEST_CASE("PluginDisplayType labels display types", "[core][plugin-display-type]")
{
    CHECK(pluginDisplayTypeLabel(PluginDisplayType::Distortion) == "Distortion");
    CHECK(pluginDisplayTypeLabel(PluginDisplayType::Eq) == "EQ");
    CHECK(pluginDisplayTypeLabel(PluginDisplayType::Uncategorized) == "Uncategorized");
}

} // namespace rock_hero::editor::core
