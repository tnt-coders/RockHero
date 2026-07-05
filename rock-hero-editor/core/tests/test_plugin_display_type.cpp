#include <catch2/catch_test_macros.hpp>
#include <optional>
#include <rock_hero/editor/core/signal_chain/plugin_display_type.h>
#include <vector>

namespace rock_hero::editor::core
{

// Verifies single direct VST3 category matches become automatic display types.
TEST_CASE(
    "PluginDisplayType classifies single direct scanner matches", "[core][plugin-display-type]")
{
    CHECK(
        classifyPluginDisplay(PluginDisplayMetadata{.category = "Fx|Delay"}).primary_type ==
        PluginDisplayType::Delay);
    CHECK(
        classifyPluginDisplay(PluginDisplayMetadata{.category = "Fx|Distortion"}).primary_type ==
        PluginDisplayType::Distortion);
    CHECK(
        classifyPluginDisplay(PluginDisplayMetadata{.category = "Fx|Dynamics"}).primary_type ==
        PluginDisplayType::Dynamics);
    CHECK(
        classifyPluginDisplay(PluginDisplayMetadata{.category = "Fx|EQ"}).primary_type ==
        PluginDisplayType::Eq);
    CHECK(
        classifyPluginDisplay(PluginDisplayMetadata{.category = "Fx|Filter"}).primary_type ==
        PluginDisplayType::Filter);
    CHECK(
        classifyPluginDisplay(PluginDisplayMetadata{.category = "Fx|Modulation"}).primary_type ==
        PluginDisplayType::Modulation);
    CHECK(
        classifyPluginDisplay(PluginDisplayMetadata{.category = "Fx|Pitch Shift"}).primary_type ==
        PluginDisplayType::Pitch);
    CHECK(
        classifyPluginDisplay(PluginDisplayMetadata{.category = "Fx|Reverb"}).primary_type ==
        PluginDisplayType::Reverb);
    CHECK(
        classifyPluginDisplay(PluginDisplayMetadata{.category = "Instrument|Synth"}).primary_type ==
        PluginDisplayType::Instrument);
}

// Verifies generic and non-standard scanner tokens do not block one recognized match.
TEST_CASE("PluginDisplayType ignores generic and unknown tokens", "[core][plugin-display-type]")
{
    const PluginDisplayClassification delay =
        classifyPluginDisplay(PluginDisplayMetadata{.category = "Fx|Stereo|Delay|Garbage"});
    CHECK(delay.primary_type == PluginDisplayType::Delay);
    CHECK(delay.scanned_types == std::vector{PluginDisplayType::Delay});

    const PluginDisplayClassification generic =
        classifyPluginDisplay(PluginDisplayMetadata{.category = "Fx|Stereo|Garbage"});
    CHECK(generic.primary_type == PluginDisplayType::Uncategorized);
    CHECK(generic.scanned_types.empty());
}

// Verifies multiple recognized scanner matches stay suggestions instead of choosing one.
TEST_CASE("PluginDisplayType leaves ambiguous matches uncategorized", "[core][plugin-display-type]")
{
    const PluginDisplayClassification classification = classifyPluginDisplay(
        PluginDisplayMetadata{.category = "Fx|Delay|Dynamics|Reverb|Garbage"});

    CHECK(classification.primary_type == PluginDisplayType::Uncategorized);
    CHECK(
        classification.scanned_types == std::vector{
                                            PluginDisplayType::Delay,
                                            PluginDisplayType::Dynamics,
                                            PluginDisplayType::Reverb,
                                        });
}

// Verifies Rock Hero-only concepts are not inferred from broad VST3 categories.
TEST_CASE("PluginDisplayType does not infer unsupported types", "[core][plugin-display-type]")
{
    CHECK(
        classifyPluginDisplay(PluginDisplayMetadata{.category = "Fx|Guitar"}).primary_type ==
        PluginDisplayType::Uncategorized);
    CHECK(
        classifyPluginDisplay(PluginDisplayMetadata{.category = "Fx|Bass"}).primary_type ==
        PluginDisplayType::Uncategorized);
    CHECK(
        classifyPluginDisplay(PluginDisplayMetadata{.category = "Fx|Restoration"}).primary_type ==
        PluginDisplayType::Uncategorized);
    CHECK(
        classifyPluginDisplay(PluginDisplayMetadata{.category = "Fx|Tools"}).primary_type ==
        PluginDisplayType::Uncategorized);
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
