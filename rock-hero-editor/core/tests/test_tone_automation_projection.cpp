#include "tone/tone_automation_projection.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <compare>
#include <cstdlib>
#include <rock_hero/common/audio/automation/i_tone_automation.h>
#include <rock_hero/common/core/song/arrangement.h>
#include <rock_hero/common/core/timeline/tempo_map.h>
#include <span>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace rock_hero::editor::core
{

namespace
{

// Minimal automation port returning fixed parameter metadata for projection tests.
struct StubToneAutomation final : public common::audio::IToneAutomation
{
    [[nodiscard]] std::expected<
        std::vector<common::audio::AutomatableParamInfo>, common::audio::ToneAutomationError>
    listAutomatableParameters(const std::string&) const override
    {
        if (fail_listing)
        {
            return std::unexpected{common::audio::ToneAutomationError{
                common::audio::ToneAutomationErrorCode::ToneNotLoaded, "not loaded"
            }};
        }
        return parameters;
    }

    [[nodiscard]] std::expected<
        std::vector<common::audio::AutomationCurvePoint>, common::audio::ToneAutomationError>
    readParameterCurve(const std::string&, const std::string&, const std::string&) const override
    {
        return std::vector<common::audio::AutomationCurvePoint>{};
    }

    [[nodiscard]] std::expected<void, common::audio::ToneAutomationError> writeParameterCurve(
        const std::string&, const std::string&, const std::string&,
        std::span<const common::audio::AutomationCurvePoint>) override
    {
        return {};
    }

    [[nodiscard]] std::expected<float, common::audio::ToneAutomationError> readParameterNormValue(
        const std::string&, const std::string&, const std::string&) const override
    {
        return 0.0F;
    }

    [[nodiscard]] std::expected<std::string, common::audio::ToneAutomationError>
    formatParameterValue(
        const std::string&, const std::string&, const std::string&, float norm_value) const override
    {
        return std::to_string(norm_value);
    }

    [[nodiscard]] std::expected<float, common::audio::ToneAutomationError> parseParameterValue(
        const std::string&, const std::string&, const std::string&,
        const std::string& text) const override
    {
        return std::strtof(text.c_str(), nullptr);
    }

    std::vector<common::audio::AutomatableParamInfo> parameters;

    bool fail_listing{false};
};

// Builds a param descriptor with every field set so designated init stays warning-clean.
[[nodiscard]] common::audio::AutomatableParamInfo makeParam(
    std::string instance_id, std::string param_id, std::string name)
{
    return common::audio::AutomatableParamInfo{
        .instance_id = std::move(instance_id),
        .param_id = std::move(param_id),
        .name = std::move(name),
        .group = {},
        .is_discrete = false,
        .labels = {},
        .default_norm_value = 0.0F,
        .current_norm_value = 0.0F,
        .plugin_name = {},
    };
}

[[nodiscard]] common::core::Arrangement makeArrangement()
{
    common::core::Arrangement arrangement;
    arrangement.tone_automation = {
        common::core::ToneParameterAutomation{
            .plugin_id = "plugin-a",
            .param_id = "gain",
            .points =
                {
                    common::core::ToneAutomationPoint{
                        .position = {.measure = 1, .beat = 1, .offset = {}},
                        .norm_value = 0.2F,
                        .curve_shape = 0.0F,
                    },
                    common::core::ToneAutomationPoint{
                        .position =
                            {.measure = 2, .beat = 1, .offset = common::core::Fraction{1, 2}},
                        .norm_value = 0.8F,
                        .curve_shape = 0.0F,
                    },
                },
        },
        common::core::ToneParameterAutomation{
            .plugin_id = "plugin-other-tone",
            .param_id = "mix",
            .points = {common::core::ToneAutomationPoint{
                .position = {.measure = 1, .beat = 1, .offset = {}},
                .norm_value = 0.5F,
                .curve_shape = 0.0F,
            }},
        },
    };
    return arrangement;
}

} // namespace

TEST_CASE("secondsAtGridPosition converts exact fractions", "[core][tone-automation]")
{
    const common::core::TempoMap tempo_map =
        common::core::TempoMap::defaultMap(common::core::TimeDuration{4.0});
    // Default map: 120 BPM 4/4, so measure 2 beat 1 + 1/2 beat = global beat 4.5 = 2.25 s.
    CHECK(
        secondsAtGridPosition(
            tempo_map,
            common::core::GridPosition{
                .measure = 2, .beat = 1, .offset = common::core::Fraction{1, 2}
            }) == Catch::Approx(2.25));
}

TEST_CASE(
    "makeToneAutomationViewState shows only the selected tone's bound lanes",
    "[core][tone-automation]")
{
    const common::core::TempoMap tempo_map =
        common::core::TempoMap::defaultMap(common::core::TimeDuration{4.0});
    const common::core::Arrangement arrangement = makeArrangement();
    StubToneAutomation port;
    port.parameters.push_back(makeParam("instance-a", "gain", "Gain"));

    const std::unordered_map<std::string, common::audio::ToneAutomationBinding> bindings{
        {"plugin-a",
         common::audio::ToneAutomationBinding{
             .instance_id = "instance-a", .tone_document_ref = "tones/x/tone.json"
         }},
        {"plugin-other-tone",
         common::audio::ToneAutomationBinding{
             .instance_id = "instance-b", .tone_document_ref = "tones/y/tone.json"
         }},
    };

    const ToneAutomationViewState state = makeToneAutomationViewState(
        arrangement, tempo_map, "tones/x/tone.json", bindings, {}, port, nullptr);

    CHECK(state.tone_document_ref == "tones/x/tone.json");
    REQUIRE(state.lanes.size() == 1);
    CHECK(state.lanes.front().instance_id == "instance-a");
    CHECK(state.lanes.front().name == "Gain");
    CHECK(state.lanes.front().resolved);
    REQUIRE(state.lanes.front().points.size() == 2);
    CHECK(state.lanes.front().points.back().seconds == Catch::Approx(2.25));
    CHECK(std::is_eq(state.lanes.front().points.back().norm_value <=> 0.8F));
    CHECK_FALSE(state.selected_point.has_value());

    // A held automation-point selection resolves to indices against the built lanes; a stale
    // one (no matching point) publishes as no selection instead of a wrong glyph.
    const AutomationPointSelection selected{
        .instance_id = "instance-a",
        .param_id = "gain",
        .position = arrangement.tone_automation.front().points.back().position,
    };
    const ToneAutomationViewState selected_state = makeToneAutomationViewState(
        arrangement, tempo_map, "tones/x/tone.json", bindings, {}, port, &selected);
    REQUIRE(selected_state.selected_point.has_value());
    CHECK(selected_state.selected_point->lane_index == 0);
    CHECK(selected_state.selected_point->point_index == 1);

    const AutomationPointSelection stale{
        .instance_id = "instance-a",
        .param_id = "gain",
        .position = {.measure = 9, .beat = 1, .offset = {}},
    };
    const ToneAutomationViewState stale_state = makeToneAutomationViewState(
        arrangement, tempo_map, "tones/x/tone.json", bindings, {}, port, &stale);
    CHECK_FALSE(stale_state.selected_point.has_value());
}

TEST_CASE(
    "makeToneAutomationViewState renders unknown parameters as unresolved lanes",
    "[core][tone-automation]")
{
    const common::core::TempoMap tempo_map =
        common::core::TempoMap::defaultMap(common::core::TimeDuration{4.0});
    const common::core::Arrangement arrangement = makeArrangement();
    const StubToneAutomation port; // No parameters listed: the plugin failed to resolve.

    const std::unordered_map<std::string, common::audio::ToneAutomationBinding> bindings{
        {"plugin-a",
         common::audio::ToneAutomationBinding{
             .instance_id = "instance-a", .tone_document_ref = "tones/x/tone.json"
         }},
    };

    const ToneAutomationViewState state = makeToneAutomationViewState(
        arrangement, tempo_map, "tones/x/tone.json", bindings, {}, port, nullptr);

    REQUIRE(state.lanes.size() == 1);
    CHECK_FALSE(state.lanes.front().resolved);
    CHECK(state.lanes.front().name == "gain");
}

TEST_CASE("makeToneAutomationViewState flags failed parameter listings", "[core][tone-automation]")
{
    const common::core::TempoMap tempo_map =
        common::core::TempoMap::defaultMap(common::core::TimeDuration{4.0});
    const common::core::Arrangement arrangement = makeArrangement();
    StubToneAutomation port;
    port.fail_listing = true;

    const ToneAutomationViewState state = makeToneAutomationViewState(
        arrangement, tempo_map, "tones/x/tone.json", {}, {}, port, nullptr);

    // A failed listing is not the same as an empty tone: the picker reports it distinctly.
    CHECK(state.parameters_unavailable);
    CHECK(state.available_parameters.empty());

    const StubToneAutomation empty_port;
    const ToneAutomationViewState empty_state = makeToneAutomationViewState(
        arrangement, tempo_map, "tones/x/tone.json", {}, {}, empty_port, nullptr);
    CHECK_FALSE(empty_state.parameters_unavailable);
}

} // namespace rock_hero::editor::core
