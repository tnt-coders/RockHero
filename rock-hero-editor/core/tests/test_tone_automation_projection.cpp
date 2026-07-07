#include "tone/tone_automation_projection.h"

#include <catch2/catch_test_macros.hpp>
#include <rock_hero/common/audio/automation/i_tone_automation.h>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace rock_hero::editor::core
{

namespace
{

// Minimal automation port returning fixed parameters and per-parameter curves for projection tests.
struct StubToneAutomation final : public common::audio::IToneAutomation
{
    [[nodiscard]] std::expected<
        std::vector<common::audio::AutomatableParamInfo>, common::audio::ToneAutomationError>
    listAutomatableParameters(const std::string&) const override
    {
        return parameters;
    }

    [[nodiscard]] std::expected<
        std::vector<common::audio::AutomationCurvePoint>, common::audio::ToneAutomationError>
    readParameterCurve(
        const std::string&, const std::string& instance_id,
        const std::string& param_id) const override
    {
        for (const auto& entry : curves)
        {
            if (entry.first.first == instance_id && entry.first.second == param_id)
            {
                return entry.second;
            }
        }
        return std::vector<common::audio::AutomationCurvePoint>{};
    }

    [[nodiscard]] std::expected<void, common::audio::ToneAutomationError> writeParameterCurve(
        const std::string&, const std::string&, const std::string&,
        std::span<const common::audio::AutomationCurvePoint>) override
    {
        return {};
    }

    std::vector<common::audio::AutomatableParamInfo> parameters;
    std::vector<std::pair<
        std::pair<std::string, std::string>, std::vector<common::audio::AutomationCurvePoint>>>
        curves;
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
    };
}

} // namespace

TEST_CASE(
    "toneAutomationViewStateFor is empty for an empty tone reference", "[core][tone-automation]")
{
    const StubToneAutomation port;
    const ToneAutomationViewState state = toneAutomationViewStateFor(port, "");
    CHECK(state.tone_document_ref.empty());
    CHECK(state.lanes.empty());
}

TEST_CASE(
    "toneAutomationViewStateFor emits a lane only for parameters that carry a curve",
    "[core][tone-automation]")
{
    StubToneAutomation port;
    port.parameters.push_back(makeParam("plug-1", "gain", "Gain"));
    port.parameters.push_back(makeParam("plug-1", "tone", "Tone"));
    port.curves.push_back(
        {{"plug-1", "gain"},
         {
             common::audio::AutomationCurvePoint{.seconds = 0.0, .norm_value = 0.2F},
             common::audio::AutomationCurvePoint{.seconds = 2.0, .norm_value = 0.8F},
         }});

    const ToneAutomationViewState state = toneAutomationViewStateFor(port, "tones/x/tone.json");

    CHECK(state.tone_document_ref == "tones/x/tone.json");
    REQUIRE(state.lanes.size() == 1);
    CHECK(state.lanes.front().instance_id == "plug-1");
    CHECK(state.lanes.front().param_id == "gain");
    CHECK(state.lanes.front().name == "Gain");
    CHECK(state.lanes.front().resolved);
    REQUIRE(state.lanes.front().points.size() == 2);
    CHECK(state.lanes.front().points.back().norm_value == 0.8F);
    CHECK(state.lanes.front().points.back().seconds == 2.0);
}

} // namespace rock_hero::editor::core
