#include <catch2/catch_test_macros.hpp>
#include <optional>
#include <rock_hero/editor/core/plugin_display_type.h>
#include <vector>

namespace rock_hero::editor::core
{

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
    const PluginDisplayClassification classification = classifyPluginDisplay(
        PluginDisplayMetadata{
            .name = "Archetype Nolly X",
            .manufacturer = "Neural DSP",
            .format_name = "VST3",
            .category = "Fx|Distortion|Dynamics|Reverb",
        });

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
        });
    CHECK(parametric_od.primary_type == PluginDisplayType::Distortion);
    CHECK(parametric_od.scanned_types.empty());
    CHECK(parametric_od.filter_types == std::vector{PluginDisplayType::Distortion});

    const PluginDisplayClassification ignite_emissary = classifyPluginDisplay(
        PluginDisplayMetadata{
            .name = "Ignite - Emissary",
            .manufacturer = "Ignite Amps",
            .format_name = "VST3",
            .category = "Fx",
        });
    CHECK(ignite_emissary.primary_type == PluginDisplayType::Amp);
    CHECK(ignite_emissary.scanned_types.empty());
    CHECK(ignite_emissary.filter_types == std::vector{PluginDisplayType::Amp});

    const PluginDisplayClassification ignite_nadir = classifyPluginDisplay(
        PluginDisplayMetadata{
            .name = "Ignite - NadIR",
            .manufacturer = "Unexpected Ignite Metadata",
            .format_name = "VST3",
            .category = "Fx",
        });
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
