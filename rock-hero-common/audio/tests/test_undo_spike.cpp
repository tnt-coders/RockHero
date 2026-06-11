// SPIKE (temporary): Phase 2 Tracktion/JUCE undo behavior spike.
//
// This file characterizes real Tracktion undo-manager and plugin-state behavior so the editor
// undo/redo design can choose a quarantine mechanism and confirm rollback feasibility
// (docs/in-progress/editor-engine-undo-master-plan-v3.md Phase 2 / Phase M, and
// editor-undo-plan.md Stage 0).
//
// It is investigative, not a regression gate: most observations are emitted with WARN so they show
// up in the Catch2 report regardless of pass/fail. The plugin-dependent case needs a real VST3 and
// is skipped unless ROCKHERO_SPIKE_PLUGIN is set to a .vst3 file path. Remove this file, the
// matching Engine spike probes, and its CMake entry using the cleanup ledger in the v3 master plan.

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <expected>
#include <filesystem>
#include <juce_core/juce_core.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <optional>
#include <rock_hero/common/audio/engine.h>
#include <rock_hero/common/audio/gain.h>
#include <rock_hero/common/audio/i_live_rig.h>
#include <rock_hero/common/audio/i_plugin_host.h>
#include <string>
#include <vector>

namespace rock_hero::common::audio
{

namespace
{

// Owns the JUCE runtime guard before constructing the real Tracktion-backed engine, matching the
// harness used by the existing engine integration tests.
class SpikeEngineHarness final
{
public:
    juce::ScopedJuceInitialiser_GUI scoped_gui;
    Engine engine;
};

// Formats one undo-manager observation as a single human-readable line for the spike report.
[[nodiscard]] std::string describeUndo(
    const std::string& label, const Engine::SpikeUndoObservation& observation)
{
    return label + ": canUndo=" + (observation.can_undo ? "true" : "false") +
           " canRedo=" + (observation.can_redo ? "true" : "false") +
           " storedUnits=" + std::to_string(observation.stored_unit_count) + " undoDesc='" +
           observation.undo_description + "'";
}

// Reads the optional reference-plugin path the spike uses for the plugin-dependent observations.
[[nodiscard]] std::optional<std::filesystem::path> spikePluginPathFromEnv()
{
    const juce::String value =
        juce::SystemStats::getEnvironmentVariable("ROCKHERO_SPIKE_PLUGIN", {});
    if (value.isEmpty())
    {
        return std::nullopt;
    }

    return std::filesystem::path{value.toStdString()};
}

// Matches the requested spike plugin against Tracktion's candidate path while tolerating Windows
// path spelling differences.
[[nodiscard]] bool isRequestedSpikePlugin(
    const PluginCandidate& plugin_candidate, const std::filesystem::path& plugin_path)
{
    std::error_code error;
    if (std::filesystem::equivalent(plugin_candidate.file_path, plugin_path, error))
    {
        return true;
    }

    return plugin_candidate.file_path.lexically_normal() == plugin_path.lexically_normal();
}

// SPIKE: Finds the exact requested plugin in the candidates already known to Tracktion. This keeps
// the run focused on one reference plugin even when JUCE's search-path scanner does not return an
// exact single-file VST3 path.
[[nodiscard]] std::optional<PluginCandidate> findRequestedSpikePlugin(
    const std::vector<PluginCandidate>& plugin_candidates, const std::filesystem::path& plugin_path)
{
    const auto candidate = std::ranges::find_if(
        plugin_candidates, [&plugin_path](const PluginCandidate& plugin_candidate) {
            return isRequestedSpikePlugin(plugin_candidate, plugin_path);
        });

    if (candidate == plugin_candidates.end())
    {
        return std::nullopt;
    }

    return *candidate;
}

// Resolves the reference plugin the same way the round-trip case does: exact single-file scan match
// first, then Tracktion's known catalog, matched against ROCKHERO_SPIKE_PLUGIN.
[[nodiscard]] std::optional<PluginCandidate> resolveReferenceCandidate(
    IPluginHost& plugin_host, const std::filesystem::path& plugin_path)
{
    if (const auto scanned = plugin_host.scanPluginLocations({plugin_path}); scanned.has_value())
    {
        if (auto match = findRequestedSpikePlugin(*scanned, plugin_path); match.has_value())
        {
            return match;
        }
    }

    return findRequestedSpikePlugin(plugin_host.knownPluginCatalog(), plugin_path);
}

// Formats a raw-role probe entry for one user plugin instance id.
[[nodiscard]] std::string userRole(const std::string& instance_id)
{
    return "user:" + instance_id;
}

// Joins instance ids into one bracketed string for a spike report line.
[[nodiscard]] std::string joinInstanceIds(const std::vector<std::string>& instance_ids)
{
    std::string joined = "[";
    for (std::size_t index = 0; index < instance_ids.size(); ++index)
    {
        joined += instance_ids[index];
        if (index + 1 < instance_ids.size())
        {
            joined += ", ";
        }
    }
    joined += "]";
    return joined;
}

} // namespace

// Probes whether ordinary live-rig edits feed Tracktion's internal undo manager, and whether
// clearing that history leaves the engine usable (quarantine-by-clearing side-effect check).
TEST_CASE(
    "Spike: Tracktion undo manager state survives clear without plugins",
    "[audio][engine][spike][integration]")
{
    SpikeEngineHarness harness;
    Engine& engine = harness.engine;
    ILiveRig& live_rig = engine;

    WARN(describeUndo("undo baseline (fresh engine)", engine.spikeObserveUndo()));

    const auto gain_set = live_rig.setOutputGain(Gain{-6.0});
    CHECK(gain_set.has_value());
    WARN(describeUndo("undo after setOutputGain(-6 dB)", engine.spikeObserveUndo()));

    engine.spikeClearUndoHistory();
    WARN(describeUndo("undo after clearUndoHistory()", engine.spikeObserveUndo()));

    // Quarantine side-effect probe: the engine must still accept edits after the undo history is
    // cleared, otherwise clear-after-mutation is not a safe quarantine mechanism.
    const auto gain_set_after_clear = live_rig.setOutputGain(Gain{-3.0});
    CHECK(gain_set_after_clear.has_value());
    CHECK(live_rig.outputGain().db < 0.0);
    WARN(describeUndo("undo after post-clear edit", engine.spikeObserveUndo()));
}

// Verifies real plugin-chain edits keep user plugins between the fixed eager structural anchors.
TEST_CASE(
    "Spike: real VST3 stays between structural anchors", "[audio][engine][spike][integration]")
{
    const std::optional<std::filesystem::path> plugin_path = spikePluginPathFromEnv();
    if (!plugin_path.has_value())
    {
        SKIP("Set ROCKHERO_SPIKE_PLUGIN to a .vst3 file path to run the anchor-order spike.");
    }
    if (!std::filesystem::exists(*plugin_path))
    {
        SKIP("ROCKHERO_SPIKE_PLUGIN does not exist: " + plugin_path->string());
    }

    SpikeEngineHarness harness;
    Engine& engine = harness.engine;
    IPluginHost& plugin_host = engine;

    const std::optional<PluginCandidate> reference_candidate =
        resolveReferenceCandidate(plugin_host, *plugin_path);
    REQUIRE(reference_candidate.has_value());

    CHECK(
        engine.spikeRawLiveRigPluginRoles() ==
        std::vector<std::string>{"input-gain", "input-meter", "output-gain", "output-meter"});

    const auto first_insert = plugin_host.insertPlugin(*reference_candidate, 0);
    REQUIRE(first_insert.has_value());
    REQUIRE(first_insert->plugins.size() == 1);
    const std::string first_id = first_insert->plugins[0].instance_id;
    CHECK(
        engine.spikeRawLiveRigPluginRoles() ==
        std::vector<std::string>{
            "input-gain", "input-meter", userRole(first_id), "output-gain", "output-meter"
        });

    const auto second_insert = plugin_host.insertPlugin(*reference_candidate, 1);
    REQUIRE(second_insert.has_value());
    REQUIRE(second_insert->plugins.size() == 2);
    const std::string second_id = second_insert->plugins[1].instance_id;
    CHECK(
        engine.spikeRawLiveRigPluginRoles() == std::vector<std::string>{
                                                   "input-gain",
                                                   "input-meter",
                                                   userRole(first_id),
                                                   userRole(second_id),
                                                   "output-gain",
                                                   "output-meter"
                                               });

    const auto moved = plugin_host.movePlugin(first_id, 1);
    REQUIRE(moved.has_value());
    CHECK(
        engine.spikeRawLiveRigPluginRoles() == std::vector<std::string>{
                                                   "input-gain",
                                                   "input-meter",
                                                   userRole(second_id),
                                                   userRole(first_id),
                                                   "output-gain",
                                                   "output-meter"
                                               });

    const auto removed = plugin_host.removePlugin(second_id);
    REQUIRE(removed.has_value());
    CHECK(
        engine.spikeRawLiveRigPluginRoles() ==
        std::vector<std::string>{
            "input-gain", "input-meter", userRole(first_id), "output-gain", "output-meter"
        });
}

// Probes undo-manager mutation across the real plugin operations undo will wrap, plus whether
// plugin-state restore preserves the runtime item id. Requires a real VST3 reference plugin.
TEST_CASE(
    "Spike: real VST3 undo manager and plugin-state id round trip",
    "[audio][engine][spike][integration]")
{
    const std::optional<std::filesystem::path> plugin_path = spikePluginPathFromEnv();
    if (!plugin_path.has_value())
    {
        SKIP("Set ROCKHERO_SPIKE_PLUGIN to a .vst3 file path to run the plugin spike.");
    }
    if (!std::filesystem::exists(*plugin_path))
    {
        SKIP("ROCKHERO_SPIKE_PLUGIN does not exist: " + plugin_path->string());
    }

    SpikeEngineHarness harness;
    Engine& engine = harness.engine;
    IPluginHost& plugin_host = engine;

    const auto candidates = plugin_host.scanPluginLocations({*plugin_path});
    if (!candidates.has_value())
    {
        FAIL(
            "Reference plugin scan failed (" << static_cast<int>(candidates.error().code)
                                             << "): " << candidates.error().message);
    }
    REQUIRE(candidates.has_value());
    std::optional<PluginCandidate> reference_candidate =
        findRequestedSpikePlugin(*candidates, *plugin_path);
    if (!reference_candidate.has_value())
    {
        reference_candidate =
            findRequestedSpikePlugin(plugin_host.knownPluginCatalog(), *plugin_path);
    }
    REQUIRE(reference_candidate.has_value());
    WARN("Reference plugin: " + reference_candidate->name + " [" + reference_candidate->id + "]");

    WARN(describeUndo("undo before any insert", engine.spikeObserveUndo()));

    const auto first_insert = plugin_host.insertPlugin(*reference_candidate, 0);
    if (!first_insert.has_value())
    {
        FAIL(
            "insertPlugin failed (" << static_cast<int>(first_insert.error().code)
                                    << "): " << first_insert.error().message);
    }
    REQUIRE(first_insert.has_value());
    REQUIRE_FALSE(first_insert->plugins.empty());
    const std::string first_instance_id = first_insert->plugins.front().instance_id;
    WARN(describeUndo("undo after insert #1", engine.spikeObserveUndo()));

    const auto second_insert = plugin_host.insertPlugin(*reference_candidate, 1);
    REQUIRE(second_insert.has_value());
    REQUIRE(second_insert->plugins.size() >= 2);
    WARN(describeUndo("undo after insert #2", engine.spikeObserveUndo()));

    // Move the first plugin after the second to observe how a reorder touches the undo manager.
    const auto moved = plugin_host.movePlugin(first_instance_id, 1);
    CHECK(moved.has_value());
    WARN(describeUndo("undo after move", engine.spikeObserveUndo()));

    ILiveRig& live_rig = engine;
    const auto gain_set = live_rig.setOutputGain(Gain{-4.0});
    CHECK(gain_set.has_value());
    WARN(describeUndo("undo after output gain (with plugins loaded)", engine.spikeObserveUndo()));

    // Q5: capture/remove/reinsert with the id stripped (current captureActiveRig() behavior). The
    // restored instance should get a new id, which is why the design assumes changed-id restore.
    const auto stripped_round_trip = engine.spikeStateRoundTrip(first_instance_id, false);
    CHECK(stripped_round_trip.error.empty());
    WARN(
        "state round trip (id stripped): original=" + stripped_round_trip.original_instance_id +
        " restored=" + stripped_round_trip.restored_instance_id + " capturedHadId=" +
        (stripped_round_trip.captured_state_had_id_property ? "true" : "false") +
        " stateBytes=" + std::to_string(stripped_round_trip.captured_state_size_bytes));
    WARN(describeUndo("  after capture", stripped_round_trip.undo_after_capture));
    WARN(describeUndo("  after remove", stripped_round_trip.undo_after_remove));
    WARN(describeUndo("  after reinsert", stripped_round_trip.undo_after_reinsert));
    if (stripped_round_trip.error.empty())
    {
        CHECK(stripped_round_trip.restored_instance_id != stripped_round_trip.original_instance_id);
    }

    // Q5 follow-up: capture/remove/reinsert keeping the id, to see whether Tracktion preserves the
    // runtime item id when it is left in the state tree.
    if (stripped_round_trip.error.empty() && !stripped_round_trip.restored_instance_id.empty())
    {
        const auto kept_round_trip =
            engine.spikeStateRoundTrip(stripped_round_trip.restored_instance_id, true);
        CHECK(kept_round_trip.error.empty());
        WARN(
            "state round trip (id kept): original=" + kept_round_trip.original_instance_id +
            " restored=" + kept_round_trip.restored_instance_id + " capturedHadId=" +
            (kept_round_trip.captured_state_had_id_property ? "true" : "false"));
        WARN(describeUndo("  after reinsert", kept_round_trip.undo_after_reinsert));
    }
}

// Option B probe: tests whether Tracktion's Edit-level undo could serve as a per-command inverse for
// RockHero undo. The expectation is that it cannot, because Edit::undo() is a positional stack pop
// over a ValueTree that RockHero is not the sole writer of, so it is not addressable to a specific
// RockHero command. Each SECTION runs on a fresh engine. Observations are recorded with WARN; the
// result is the finding, so the test does not assert a particular undo outcome.
TEST_CASE(
    "Spike: Tracktion Edit-level undo is not addressable to a RockHero command",
    "[audio][engine][spike][integration]")
{
    const std::optional<std::filesystem::path> plugin_path = spikePluginPathFromEnv();
    if (!plugin_path.has_value())
    {
        SKIP("Set ROCKHERO_SPIKE_PLUGIN to a .vst3 file path to run the Option B spike.");
    }
    if (!std::filesystem::exists(*plugin_path))
    {
        SKIP("ROCKHERO_SPIKE_PLUGIN does not exist: " + plugin_path->string());
    }

    SpikeEngineHarness harness;
    Engine& engine = harness.engine;
    IPluginHost& plugin_host = engine;
    ILiveRig& live_rig = engine;

    const std::optional<PluginCandidate> reference_candidate =
        resolveReferenceCandidate(plugin_host, *plugin_path);
    REQUIRE(reference_candidate.has_value());

    SECTION("clean-pop: one Edit::undo() after a wrapped RockHero insert")
    {
        // Can a single Edit::undo() cleanly reverse exactly one RockHero insert, or does it leave a
        // partial state because the insert spans several internal Tracktion transactions?
        engine.spikeClearUndoHistory();
        engine.spikeBeginUndoTransaction("rh-insert");

        const auto insert = plugin_host.insertPlugin(*reference_candidate, 0);
        REQUIRE(insert.has_value());
        REQUIRE_FALSE(insert->plugins.empty());
        const std::string inserted_id = insert->plugins.front().instance_id;
        const auto ids_after_insert = engine.spikeUserPluginInstanceIds();
        WARN(
            "clean-pop: inserted id=" + inserted_id +
            " chain=" + joinInstanceIds(ids_after_insert));
        WARN(describeUndo("clean-pop: after wrapped insert", engine.spikeObserveUndo()));

        const bool undo_performed = engine.spikeTracktionUndo();
        const auto ids_after_undo = engine.spikeUserPluginInstanceIds();
        WARN(
            "clean-pop: Edit::undo() performed=" + std::string{undo_performed ? "true" : "false"} +
            " chain after undo=" + joinInstanceIds(ids_after_undo));
        WARN(describeUndo("clean-pop: after Edit::undo()", engine.spikeObserveUndo()));

        const bool redo_performed = engine.spikeTracktionRedo();
        const auto ids_after_redo = engine.spikeUserPluginInstanceIds();
        WARN(
            "clean-pop: Edit::redo() performed=" + std::string{redo_performed ? "true" : "false"} +
            " chain after redo=" + joinInstanceIds(ids_after_redo) + " (original inserted id was " +
            inserted_id + ")");
    }

    SECTION("wrong-target: Edit::undo() pops the last write, not the chosen command")
    {
        // Insert a plugin in one transaction, then change output gain in a later transaction. A
        // single Edit::undo() should revert the gain (the most-recent transaction), not the insert,
        // proving Edit::undo() is positional rather than addressable to a chosen RockHero command.
        engine.spikeClearUndoHistory();
        engine.spikeBeginUndoTransaction("rh-insert");
        const auto insert = plugin_host.insertPlugin(*reference_candidate, 0);
        REQUIRE(insert.has_value());
        REQUIRE_FALSE(insert->plugins.empty());
        const std::string inserted_id = insert->plugins.front().instance_id;
        const std::size_t chain_size_before = engine.spikeUserPluginInstanceIds().size();

        const double gain_before_db = live_rig.outputGain().db;
        engine.spikeBeginUndoTransaction("rh-gain");
        const auto gain_changed = live_rig.setOutputGain(Gain{gain_before_db - 5.0});
        REQUIRE(gain_changed.has_value());
        const double gain_after_change_db = live_rig.outputGain().db;

        const bool undo_performed = engine.spikeTracktionUndo();
        const double gain_after_undo_db = live_rig.outputGain().db;
        const std::size_t chain_size_after_undo = engine.spikeUserPluginInstanceIds().size();
        WARN(
            "wrong-target: Edit::undo() performed=" +
            std::string{undo_performed ? "true" : "false"} +
            " | gain before=" + std::to_string(gain_before_db) +
            " afterChange=" + std::to_string(gain_after_change_db) +
            " afterUndo=" + std::to_string(gain_after_undo_db) +
            " | chain before=" + std::to_string(chain_size_before) + " afterUndo=" +
            std::to_string(chain_size_after_undo) + " (inserted id " + inserted_id + ")");
    }
}

} // namespace rock_hero::common::audio
