#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <compare>
#include <rock_hero/common/audio/automation/tone_automation_rebuild.h>
#include <rock_hero/common/audio/live_rig/i_live_rig.h>
#include <rock_hero/common/audio/testing/fake_tone_automation.h>
#include <rock_hero/common/core/timeline/fraction.h>
#include <rock_hero/common/core/timeline/tempo_map.h>
#include <rock_hero/common/core/timeline/timeline.h>
#include <rock_hero/common/core/tone/tone_automation.h>
#include <string>
#include <unordered_map>
#include <vector>

namespace rock_hero::common::audio
{

namespace
{

using testing::FakeToneAutomation;
using testing::ToneAutomationWriteCall;

// A two-point musical curve on the given plugin id, spanning a sub-beat position.
[[nodiscard]] common::core::ToneParameterAutomation makeAutomation(
    std::string plugin_id, std::string param_id)
{
    return common::core::ToneParameterAutomation{
        .plugin_id = std::move(plugin_id),
        .param_id = std::move(param_id),
        .points = {
            common::core::ToneAutomationPoint{
                .position = common::core::GridPosition{.measure = 1, .beat = 1, .offset = {}},
                .norm_value = 0.25F,
                .curve_shape = 0.0F,
            },
            common::core::ToneAutomationPoint{
                .position =
                    common::core::GridPosition{
                        .measure = 2,
                        .beat = 3,
                        .offset = common::core::Fraction{1, 2},
                    },
                .norm_value = 0.75F,
                .curve_shape = -0.5F,
            },
        },
    };
}

} // namespace

TEST_CASE(
    "makeToneAutomationBindings maps durable ids to loaded instances", "[audio][tone-automation]")
{
    const std::vector<LoadedToneChainIdentities> tone_chains{
        LoadedToneChainIdentities{
            .tone_document_ref = "tones/x/tone.json",
            .plugins =
                {
                    LoadedTonePluginIdentity{.instance_id = "instance-1", .stable_id = "plugin-a"},
                    // No minted durable id: persisted automation can never reference this plugin.
                    LoadedTonePluginIdentity{.instance_id = "instance-2", .stable_id = ""},
                },
            .summed_reported_latency_seconds = 0.0,
        },
        LoadedToneChainIdentities{
            .tone_document_ref = "tones/y/tone.json",
            .plugins =
                {
                    LoadedTonePluginIdentity{.instance_id = "instance-3", .stable_id = "plugin-b"},
                },
            .summed_reported_latency_seconds = 0.0,
        },
    };

    const std::unordered_map<std::string, ToneAutomationBinding> bindings =
        makeToneAutomationBindings(tone_chains);

    REQUIRE(bindings.size() == 2);
    CHECK(bindings.at("plugin-a").instance_id == "instance-1");
    CHECK(bindings.at("plugin-a").tone_document_ref == "tones/x/tone.json");
    CHECK(bindings.at("plugin-b").instance_id == "instance-3");
    CHECK(bindings.at("plugin-b").tone_document_ref == "tones/y/tone.json");
}

TEST_CASE(
    "derivedToneCurvePoints resolves musical positions through the tempo map",
    "[audio][tone-automation]")
{
    const common::core::TempoMap tempo_map =
        common::core::TempoMap::defaultMap(common::core::TimeDuration{4.0});
    const common::core::ToneParameterAutomation automation = makeAutomation("plugin-a", "gain");

    const std::vector<AutomationCurvePoint> points =
        derivedToneCurvePoints(tempo_map, automation.points);

    REQUIRE(points.size() == 2);
    CHECK(points.front().seconds == Catch::Approx(tempo_map.secondsAtNote(1, 1, {})));
    CHECK(std::is_eq(points.front().norm_value <=> 0.25F));
    CHECK(std::is_eq(points.front().curve_shape <=> 0.0F));
    CHECK(
        points.back().seconds ==
        Catch::Approx(tempo_map.secondsAtNote(2, 3, common::core::Fraction{1, 2})));
    CHECK(std::is_eq(points.back().norm_value <=> 0.75F));
    CHECK(std::is_eq(points.back().curve_shape <=> -0.5F));
}

TEST_CASE(
    "rebuildToneAutomationCurves writes bound entries and skips unresolved ones",
    "[audio][tone-automation]")
{
    const common::core::TempoMap tempo_map =
        common::core::TempoMap::defaultMap(common::core::TimeDuration{4.0});
    const std::vector<common::core::ToneParameterAutomation> automation{
        makeAutomation("plugin-a", "gain"),
        // No binding carries this durable id, so the entry must be skipped.
        makeAutomation("plugin-unknown", "mix"),
        // Bound but not currently loaded (empty instance), so the entry must be skipped.
        makeAutomation("plugin-unloaded", "drive"),
    };
    const std::unordered_map<std::string, ToneAutomationBinding> bindings{
        {"plugin-a",
         ToneAutomationBinding{
             .instance_id = "instance-1", .tone_document_ref = "tones/x/tone.json"
         }},
        {"plugin-unloaded",
         ToneAutomationBinding{.instance_id = "", .tone_document_ref = "tones/y/tone.json"}},
    };
    FakeToneAutomation port;

    rebuildToneAutomationCurves(port, automation, tempo_map, bindings);

    REQUIRE(port.write_calls.size() == 1);
    const ToneAutomationWriteCall& call = port.write_calls.front();
    CHECK(call.tone_document_ref == "tones/x/tone.json");
    CHECK(call.instance_id == "instance-1");
    CHECK(call.param_id == "gain");
    REQUIRE(call.points.size() == 2);
    CHECK(call.points.front().seconds == Catch::Approx(tempo_map.secondsAtNote(1, 1, {})));
}

TEST_CASE(
    "rebuildToneAutomationCurves continues past an individual write failure",
    "[audio][tone-automation]")
{
    const common::core::TempoMap tempo_map =
        common::core::TempoMap::defaultMap(common::core::TimeDuration{4.0});
    const std::vector<common::core::ToneParameterAutomation> automation{
        makeAutomation("plugin-a", "gain"),
        makeAutomation("plugin-b", "mix"),
    };
    const std::unordered_map<std::string, ToneAutomationBinding> bindings{
        {"plugin-a",
         ToneAutomationBinding{
             .instance_id = "instance-1", .tone_document_ref = "tones/x/tone.json"
         }},
        {"plugin-b",
         ToneAutomationBinding{
             .instance_id = "instance-2", .tone_document_ref = "tones/y/tone.json"
         }},
    };
    FakeToneAutomation port;
    port.fail_writes_for_param = "gain";

    rebuildToneAutomationCurves(port, automation, tempo_map, bindings);

    // The failed write is best-effort (logged and skipped); the next entry still lands.
    REQUIRE(port.write_calls.size() == 2);
    CHECK(port.write_calls.front().param_id == "gain");
    CHECK(port.write_calls.back().param_id == "mix");
}

} // namespace rock_hero::common::audio
