#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <expected>
#include <filesystem>
#include <optional>
#include <rock_hero/common/audio/plugin_chain_limits.h>
#include <rock_hero/common/audio/testing/recording_plugin_host.h>
#include <string>
#include <utility>
#include <vector>

namespace rock_hero::common::audio
{

namespace
{

// Builds a compact candidate fixture for plugin-host boundary tests.
[[nodiscard]] PluginCandidate makeCandidate(std::string id, std::string name)
{
    return PluginCandidate{
        .id = std::move(id),
        .name = std::move(name),
        .manufacturer = "Rock Hero Tests",
        .format_name = "VST3",
        .file_path = std::filesystem::path{"Plugin.vst3"},
    };
}

// Builds a compact chain-entry fixture with a matching runtime and plugin identity.
[[nodiscard]] PluginChainEntry makeChainEntry(
    std::string instance_id, std::string plugin_id, std::size_t chain_index)
{
    return PluginChainEntry{
        .instance_id = std::move(instance_id),
        .plugin_id = std::move(plugin_id),
        .name = "Plugin " + std::to_string(chain_index),
        .manufacturer = "Rock Hero Tests",
        .format_name = "VST3",
        .chain_index = chain_index,
    };
}

} // namespace

// Verifies default catalog scans refresh the host-owned known catalog.
TEST_CASE("IPluginHost refreshes default plugin catalog", "[audio][plugin-host]")
{
    testing::RecordingPluginHost plugin_host;

    const auto scan_result = plugin_host.scanPluginCatalog();
    const std::vector<PluginCandidate> candidates = plugin_host.knownPluginCatalog();

    REQUIRE(scan_result.has_value());
    REQUIRE(candidates.size() == 1);
    CHECK(candidates.front().id == "catalog-plugin-id");
    CHECK(plugin_host.last_scan_roots.empty());
    CHECK(plugin_host.catalog_scan_call_count == 1);
    CHECK(plugin_host.known_candidates_call_count == 1);
}

// Verifies catalog scans expose scanned plugin candidates without selecting a single file.
TEST_CASE("IPluginHost scans plugin catalog locations", "[audio][plugin-host]")
{
    testing::RecordingPluginHost plugin_host;
    const std::vector<std::filesystem::path> roots{std::filesystem::path{"VST3"}};

    const auto candidates = plugin_host.scanPluginLocations(roots);

    REQUIRE(candidates.has_value());
    REQUIRE(candidates->size() == 1);
    CHECK(candidates->front().id == "catalog-plugin-id");
    CHECK(plugin_host.last_scan_roots == roots);
    CHECK(plugin_host.catalog_scan_call_count == 1);
}

// Verifies known candidates can be displayed without scanning plugin folders.
TEST_CASE("IPluginHost returns known plugin candidates", "[audio][plugin-host]")
{
    const testing::RecordingPluginHost plugin_host;

    const std::vector<PluginCandidate> candidates = plugin_host.knownPluginCatalog();

    REQUIRE(candidates.size() == 1);
    CHECK(candidates.front().id == "catalog-plugin-id");
    CHECK(plugin_host.known_candidates_call_count == 1);
    CHECK(plugin_host.catalog_scan_call_count == 0);
}

// Verifies selected plugin candidates can be inserted at any visible chain slot.
TEST_CASE("IPluginHost inserts plugins at visible chain positions", "[audio][plugin-host]")
{
    testing::RecordingPluginHost plugin_host;
    const PluginCandidate amp_candidate{
        .id = "vst3:amp",
        .name = "Amp",
        .manufacturer = "Rock Hero Tests",
        .format_name = "VST3",
        .file_path = std::filesystem::path{"Amp.vst3"},
    };
    const PluginCandidate cab_candidate{
        .id = "vst3:cab",
        .name = "Cab",
        .manufacturer = "Rock Hero Tests",
        .format_name = "VST3",
        .file_path = std::filesystem::path{"Cab.vst3"},
    };
    const PluginCandidate drive_candidate{
        .id = "vst3:drive",
        .name = "Drive",
        .manufacturer = "Rock Hero Tests",
        .format_name = "VST3",
        .file_path = std::filesystem::path{"Drive.vst3"},
    };

    plugin_host.next_instance_id = "amp-instance";
    const auto first_result = plugin_host.insertPlugin(amp_candidate, 0);
    plugin_host.next_instance_id = "cab-instance";
    const auto append_result = plugin_host.insertPlugin(cab_candidate, 1);
    plugin_host.next_instance_id = "drive-instance";
    const auto middle_result = plugin_host.insertPlugin(drive_candidate, 1);

    REQUIRE(first_result.has_value());
    REQUIRE(append_result.has_value());
    REQUIRE(middle_result.has_value());
    REQUIRE(middle_result->snapshot.plugins.size() == 3);
    CHECK(first_result->inserted_instance_id == "amp-instance");
    CHECK(append_result->inserted_instance_id == "cab-instance");
    CHECK(middle_result->inserted_instance_id == "drive-instance");
    CHECK(middle_result->snapshot.plugins[0].instance_id == "amp-instance");
    CHECK(middle_result->snapshot.plugins[0].chain_index == 0);
    CHECK(middle_result->snapshot.plugins[1].instance_id == "drive-instance");
    CHECK(middle_result->snapshot.plugins[1].plugin_id == "vst3:drive");
    CHECK(middle_result->snapshot.plugins[1].chain_index == 1);
    CHECK(middle_result->snapshot.plugins[2].instance_id == "cab-instance");
    CHECK(middle_result->snapshot.plugins[2].chain_index == 2);
    CHECK(plugin_host.last_inserted_plugin_candidate == std::optional{drive_candidate});
    CHECK(plugin_host.last_insert_index == std::optional<std::size_t>{1});
    CHECK(plugin_host.insert_call_count == 3);
}

// Verifies plugin-host failures cross the port as typed errors.
TEST_CASE("IPluginHost insert can fail with a typed error", "[audio][plugin-host]")
{
    testing::RecordingPluginHost plugin_host;
    plugin_host.next_insert_error = PluginHostError{PluginHostErrorCode::PluginNotFound};
    const PluginCandidate selected_candidate{
        .id = "missing",
        .name = "Missing",
        .manufacturer = {},
        .format_name = "VST3",
        .file_path = {},
    };

    const auto snapshot = plugin_host.insertPlugin(selected_candidate, 0);

    REQUIRE_FALSE(snapshot.has_value());
    CHECK(snapshot.error().code == PluginHostErrorCode::PluginNotFound);
    CHECK(plugin_host.last_inserted_plugin_candidate == std::optional{selected_candidate});
    CHECK(plugin_host.insert_call_count == 1);
}

// Verifies invalid insertion slots are rejected without changing the visible chain.
TEST_CASE("IPluginHost rejects invalid insert positions", "[audio][plugin-host]")
{
    testing::RecordingPluginHost plugin_host;
    const PluginCandidate selected_candidate{
        .id = "vst3:amp",
        .name = "Amp",
        .manufacturer = "Rock Hero Tests",
        .format_name = "VST3",
        .file_path = {},
    };

    const auto snapshot = plugin_host.insertPlugin(selected_candidate, 1);

    REQUIRE_FALSE(snapshot.has_value());
    CHECK(snapshot.error().code == PluginHostErrorCode::InvalidChainIndex);
    CHECK(plugin_host.chain.empty());
}

// Verifies the hosted chain rejects insertions once the product plugin cap is reached.
TEST_CASE("IPluginHost rejects inserts at plugin limit", "[audio][plugin-host]")
{
    testing::RecordingPluginHost plugin_host;
    for (std::size_t index = 0; index < max_signal_chain_plugins; ++index)
    {
        plugin_host.chain.push_back(
            PluginChainEntry{
                .instance_id = "instance-" + std::to_string(index),
                .plugin_id = "plugin-" + std::to_string(index),
                .name = "Plugin " + std::to_string(index),
                .chain_index = index,
            });
    }
    const PluginCandidate selected_candidate{
        .id = "vst3:extra",
        .name = "Extra",
        .manufacturer = "Rock Hero Tests",
        .format_name = "VST3",
        .file_path = {},
    };

    const auto snapshot = plugin_host.insertPlugin(selected_candidate, max_signal_chain_plugins);

    REQUIRE_FALSE(snapshot.has_value());
    CHECK(snapshot.error().code == PluginHostErrorCode::PluginChainLimitExceeded);
    CHECK(plugin_host.chain.size() == max_signal_chain_plugins);
}

// Verifies loaded plugin instances can move up, down, and no-op in the visible chain.
TEST_CASE("IPluginHost moves plugin instances", "[audio][plugin-host]")
{
    testing::RecordingPluginHost plugin_host;
    plugin_host.chain = {
        PluginChainEntry{
            .instance_id = "amp-instance",
            .plugin_id = "amp",
            .name = "Amp",
            .chain_index = 0,
        },
        PluginChainEntry{
            .instance_id = "drive-instance",
            .plugin_id = "drive",
            .name = "Drive",
            .chain_index = 1,
        },
        PluginChainEntry{
            .instance_id = "cab-instance",
            .plugin_id = "cab",
            .name = "Cab",
            .chain_index = 2,
        },
    };

    const auto move_down_snapshot = plugin_host.movePlugin("amp-instance", 2);
    const auto move_up_snapshot = plugin_host.movePlugin("cab-instance", 0);
    const auto noop_snapshot = plugin_host.movePlugin("cab-instance", 0);

    REQUIRE(move_down_snapshot.has_value());
    REQUIRE(move_up_snapshot.has_value());
    REQUIRE(noop_snapshot.has_value());
    REQUIRE(noop_snapshot->plugins.size() == 3);
    CHECK(noop_snapshot->plugins[0].instance_id == "cab-instance");
    CHECK(noop_snapshot->plugins[0].chain_index == 0);
    CHECK(noop_snapshot->plugins[1].instance_id == "drive-instance");
    CHECK(noop_snapshot->plugins[1].chain_index == 1);
    CHECK(noop_snapshot->plugins[2].instance_id == "amp-instance");
    CHECK(noop_snapshot->plugins[2].chain_index == 2);
    CHECK(plugin_host.last_moved_instance_id == std::optional<std::string>{"cab-instance"});
    CHECK(plugin_host.last_move_destination_index == std::optional<std::size_t>{0});
    CHECK(plugin_host.move_call_count == 3);
}

// Verifies invalid move requests return typed errors before changing the visible chain.
TEST_CASE("IPluginHost rejects invalid plugin moves", "[audio][plugin-host]")
{
    testing::RecordingPluginHost plugin_host;
    plugin_host.chain = {
        PluginChainEntry{
            .instance_id = "amp-instance",
            .plugin_id = "amp",
            .name = "Amp",
            .chain_index = 0,
        },
    };

    const auto missing = plugin_host.movePlugin("missing-instance", 0);
    const auto invalid_index = plugin_host.movePlugin("amp-instance", 1);

    REQUIRE_FALSE(missing.has_value());
    CHECK(missing.error().code == PluginHostErrorCode::PluginInstanceNotFound);
    REQUIRE_FALSE(invalid_index.has_value());
    CHECK(invalid_index.error().code == PluginHostErrorCode::InvalidChainIndex);
    REQUIRE(plugin_host.chain.size() == 1);
    CHECK(plugin_host.chain[0].instance_id == "amp-instance");
}

// Verifies loaded plugin instances are removed through opaque instance IDs.
TEST_CASE("IPluginHost removes a plugin instance", "[audio][plugin-host]")
{
    testing::RecordingPluginHost plugin_host;
    plugin_host.chain = {
        PluginChainEntry{
            .instance_id = "instance-1",
            .plugin_id = "amp",
            .name = "Amp",
            .chain_index = 0,
        },
        PluginChainEntry{
            .instance_id = "instance-2",
            .plugin_id = "cab",
            .name = "Cab",
            .chain_index = 1,
        },
    };

    const auto result = plugin_host.removePlugin("instance-1");

    REQUIRE(result.has_value());
    REQUIRE(result->plugins.size() == 1);
    CHECK(result->plugins[0].instance_id == "instance-2");
    CHECK(result->plugins[0].chain_index == 0);
    CHECK(plugin_host.last_removed_instance_id == std::optional<std::string>{"instance-1"});
    CHECK(plugin_host.remove_call_count == 1);
}

// Verifies insert failures after side effects repair the fake back to the exact pre-call chain.
TEST_CASE("IPluginHost rolls back insert failures after mutation", "[audio][plugin-host]")
{
    testing::RecordingPluginHost plugin_host;
    plugin_host.chain = {
        makeChainEntry("amp-instance", "amp", 0),
    };
    const std::vector<PluginChainEntry> original_chain = plugin_host.chain;
    plugin_host.next_instance_id = "drive-instance";
    plugin_host.next_insert_after_mutation_error =
        PluginHostError{PluginHostErrorCode::PluginInsertionFailed};

    const auto result = plugin_host.insertPlugin(makeCandidate("drive", "Drive"), 1);

    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().code == PluginHostErrorCode::PluginInsertionFailed);
    CHECK(plugin_host.chain == original_chain);
}

// Verifies move failures after side effects repair the fake back to the exact pre-call chain.
TEST_CASE("IPluginHost rolls back move failures after mutation", "[audio][plugin-host]")
{
    testing::RecordingPluginHost plugin_host;
    plugin_host.chain = {
        makeChainEntry("amp-instance", "amp", 0),
        makeChainEntry("drive-instance", "drive", 1),
        makeChainEntry("cab-instance", "cab", 2),
    };
    const std::vector<PluginChainEntry> original_chain = plugin_host.chain;
    plugin_host.next_move_after_mutation_error =
        PluginHostError{PluginHostErrorCode::PluginMoveFailed};

    const auto result = plugin_host.movePlugin("amp-instance", 2);

    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().code == PluginHostErrorCode::PluginMoveFailed);
    CHECK(plugin_host.chain == original_chain);
}

// Verifies remove failures after side effects repair the fake back to the exact pre-call chain.
TEST_CASE("IPluginHost rolls back remove failures after mutation", "[audio][plugin-host]")
{
    testing::RecordingPluginHost plugin_host;
    plugin_host.chain = {
        makeChainEntry("amp-instance", "amp", 0),
        makeChainEntry("drive-instance", "drive", 1),
    };
    const std::vector<PluginChainEntry> original_chain = plugin_host.chain;
    plugin_host.next_remove_after_mutation_error =
        PluginHostError{PluginHostErrorCode::PluginRemovalFailed};

    const auto result = plugin_host.removePlugin("drive-instance");

    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().code == PluginHostErrorCode::PluginRemovalFailed);
    CHECK(plugin_host.chain == original_chain);
}

// Verifies the fake host's opaque plugin-state mementos recreate equivalent plugins.
TEST_CASE("IPluginHost recreates plugin instance state", "[audio][plugin-host]")
{
    testing::RecordingPluginHost plugin_host;
    plugin_host.chain = {
        PluginChainEntry{
            .instance_id = "amp-instance",
            .plugin_id = "amp",
            .name = "Amp",
            .manufacturer = "Rock Hero Tests",
            .format_name = "VST3",
            .category = "Fx|Distortion",
            .chain_index = 0,
            .block_index = 4,
            .display_type_override = "compact",
        },
    };
    const auto captured = plugin_host.capturePluginState("amp-instance");
    REQUIRE(captured.has_value());
    REQUIRE(plugin_host.removePlugin("amp-instance").has_value());

    const auto restored = plugin_host.recreatePluginStatePreservingId(*captured, 0);

    REQUIRE(restored.has_value());
    REQUIRE(restored->plugins.size() == 1);
    CHECK(restored->plugins[0].instance_id == "amp-instance");
    CHECK(restored->plugins[0].plugin_id == "amp");
    CHECK(restored->plugins[0].block_index == 4);
    CHECK(restored->plugins[0].display_type_override == "compact");
}

// Verifies id-preserving recreate is valid only after the original instance is absent.
TEST_CASE("IPluginHost rejects duplicate state recreate ids", "[audio][plugin-host]")
{
    testing::RecordingPluginHost plugin_host;
    plugin_host.chain = {
        makeChainEntry("amp-instance", "amp", 0),
    };
    const auto captured = plugin_host.capturePluginState("amp-instance");
    REQUIRE(captured.has_value());
    const std::vector<PluginChainEntry> original_chain = plugin_host.chain;

    const auto result = plugin_host.recreatePluginStatePreservingId(*captured, 1);

    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().code == PluginHostErrorCode::PluginStateRestoreFailed);
    CHECK(plugin_host.chain == original_chain);
}

// Verifies invalid opaque state bytes are rejected before mutating the fake chain.
TEST_CASE("IPluginHost rejects bad plugin state without mutation", "[audio][plugin-host]")
{
    testing::RecordingPluginHost plugin_host;
    plugin_host.chain = {
        makeChainEntry("amp-instance", "amp", 0),
    };
    const std::vector<PluginChainEntry> original_chain = plugin_host.chain;
    const PluginInstanceState invalid_state{
        .opaque_data = {std::byte{0x01}, std::byte{0x02}},
    };

    const auto insert_result = plugin_host.recreatePluginStatePreservingId(invalid_state, 1);
    const auto set_result = plugin_host.setPluginState("amp-instance", invalid_state);

    REQUIRE_FALSE(insert_result.has_value());
    CHECK(insert_result.error().code == PluginHostErrorCode::PluginStateRestoreFailed);
    REQUIRE_FALSE(set_result.has_value());
    CHECK(set_result.error().code == PluginHostErrorCode::PluginStateRestoreFailed);
    CHECK(plugin_host.chain == original_chain);
}

// Verifies state-recreate failures after side effects repair the fake's chain and state map.
TEST_CASE("IPluginHost rolls back state recreate failures", "[audio][plugin-host]")
{
    testing::RecordingPluginHost plugin_host;
    plugin_host.chain = {
        makeChainEntry("amp-instance", "amp", 0),
        makeChainEntry("drive-instance", "drive", 1),
    };
    const auto captured = plugin_host.capturePluginState("amp-instance");
    REQUIRE(captured.has_value());
    REQUIRE(plugin_host.removePlugin("amp-instance").has_value());
    const auto surviving_state = plugin_host.capturePluginState("drive-instance");
    REQUIRE(surviving_state.has_value());
    const std::vector<PluginChainEntry> original_chain = plugin_host.chain;
    plugin_host.next_recreate_state_after_mutation_error =
        PluginHostError{PluginHostErrorCode::PluginStateRestoreFailed};

    const auto result = plugin_host.recreatePluginStatePreservingId(*captured, 0);
    const auto after_failure = plugin_host.capturePluginState("drive-instance");

    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().code == PluginHostErrorCode::PluginStateRestoreFailed);
    CHECK(plugin_host.chain == original_chain);
    REQUIRE(after_failure.has_value());
    CHECK(*after_failure == *surviving_state);
}

// Verifies in-place state failures after side effects restore the exact previous state.
TEST_CASE("IPluginHost rolls back in-place state failures", "[audio][plugin-host]")
{
    testing::RecordingPluginHost plugin_host;
    plugin_host.chain = {
        makeChainEntry("amp-instance", "amp", 0),
        makeChainEntry("drive-instance", "drive", 1),
    };
    const auto original_state = plugin_host.capturePluginState("amp-instance");
    const auto replacement_state = plugin_host.capturePluginState("drive-instance");
    REQUIRE(original_state.has_value());
    REQUIRE(replacement_state.has_value());
    const std::vector<PluginChainEntry> original_chain = plugin_host.chain;
    plugin_host.next_set_state_after_mutation_error =
        PluginHostError{PluginHostErrorCode::PluginStateRestoreFailed};

    const auto result = plugin_host.setPluginState("amp-instance", *replacement_state);
    const auto after_failure = plugin_host.capturePluginState("amp-instance");

    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().code == PluginHostErrorCode::PluginStateRestoreFailed);
    CHECK(plugin_host.chain == original_chain);
    REQUIRE(after_failure.has_value());
    CHECK(*after_failure == *original_state);
}

// Verifies plugin-edit flush emits pending notifications and completed state edits.
TEST_CASE("IPluginHost flushes pending plugin edits", "[audio][plugin-host]")
{
    testing::RecordingPluginHost plugin_host;
    plugin_host.chain = {
        makeChainEntry("amp-instance", "amp", 0),
        makeChainEntry("drive-instance", "drive", 1),
    };
    const auto before_state = plugin_host.capturePluginState("amp-instance");
    PluginChainEntry after_entry = plugin_host.chain[0];
    after_entry.name = "amp preset";
    testing::RecordingPluginHost after_host;
    after_host.chain = {after_entry};
    const auto after_state = after_host.capturePluginState("amp-instance");
    REQUIRE(before_state.has_value());
    REQUIRE(after_state.has_value());

    std::vector<bool> pending_notifications;
    std::vector<PluginStateEdit> completed_edits;
    plugin_host.setPluginEditObserver(
        PluginEditObserver{
            .pending_changed = [&pending_notifications](bool pending) {
                pending_notifications.push_back(pending);
            },
        });
    plugin_host.setPluginStateEditObserver(
        PluginStateEditObserver{
            .edit_completed = [&completed_edits](PluginStateEdit edit) {
                completed_edits.push_back(std::move(edit));
            },
        });

    plugin_host.queuePendingPluginStateEdit(
        PluginStateEdit{
            .instance_id = "amp-instance",
            .before = *before_state,
            .after = *after_state,
            .label_hint = "Amp",
        });
    plugin_host.flushPendingPluginEdits();

    CHECK_FALSE(plugin_host.hasPendingPluginEdits());
    CHECK(plugin_host.flush_pending_plugin_edits_call_count == 1);
    CHECK(pending_notifications == std::vector<bool>{true, false});
    REQUIRE(completed_edits.size() == 1);
    CHECK(completed_edits[0].instance_id == "amp-instance");
    CHECK(completed_edits[0].before == *before_state);
    CHECK(completed_edits[0].after == *after_state);
    CHECK(completed_edits[0].label_hint == "Amp");
}

// Verifies hosted plugin-window command callbacks can be installed and emitted by the port fake.
TEST_CASE("IPluginHost forwards plugin window commands", "[audio][plugin-host]")
{
    testing::RecordingPluginHost plugin_host;
    int undo_count = 0;
    int redo_count = 0;
    int play_pause_count = 0;
    plugin_host.setPluginWindowCommandObserver(
        PluginWindowCommandObserver{
            .undo_requested = [&undo_count] { undo_count += 1; },
            .redo_requested = [&redo_count] { redo_count += 1; },
            .play_pause_requested = [&play_pause_count] { play_pause_count += 1; },
        });

    plugin_host.notifyPluginWindowUndoRequested();
    plugin_host.notifyPluginWindowRedoRequested();
    plugin_host.notifyPluginWindowPlayPauseRequested();

    CHECK(undo_count == 1);
    CHECK(redo_count == 1);
    CHECK(play_pause_count == 1);
}

// Verifies loaded plugin instances expose a message-thread window operation through the port.
TEST_CASE("IPluginHost opens a plugin window", "[audio][plugin-host]")
{
    testing::RecordingPluginHost plugin_host;

    const auto result = plugin_host.openPluginWindow("instance-1");

    REQUIRE(result.has_value());
    CHECK(plugin_host.last_opened_instance_id == std::optional<std::string>{"instance-1"});
    CHECK(plugin_host.open_call_count == 1);
}

} // namespace rock_hero::common::audio
