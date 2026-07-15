// Pins the Tracktion behavior the editor undo design depends on: a plugin re-created from its
// captured state keeps its original itemID as long as the state's `id` property is left intact.
//
// The editor undo stack preserves instance ids across remove-undo and insert-redo instead of
// remapping them (see docs/plans/completed/editor-undo/editor-undo-plan.md, "Instance-Id Preservation").
// That only holds because Tracktion's EditItemID::readOrCreateNewID returns any id present in the
// inserted state verbatim and allocates a fresh id only when none is present. If a vendored-engine
// upgrade changed that, instance ids would silently diverge on recreate and the undo stack would
// reference dead plugins; this test fails first.
//
// Instance-id allocation runs through the same EditItem path for built-in and external plugins, so a
// built-in VolumeAndPanPlugin is a valid proxy (this exercises id identity, not VST3 audio-state).

#include <catch2/catch_test_macros.hpp>
#include <memory>
#include <tracktion_engine/tracktion_engine.h>

namespace rock_hero::common::audio
{

namespace
{

// Owns the minimum Tracktion objects needed to drive a real plugin list in a test.
struct PluginIdPreservationHarness
{
    juce::ScopedJuceInitialiser_GUI gui;
    tracktion::Engine engine{"RockHeroPluginIdPreservation"};
    std::unique_ptr<tracktion::Edit> edit{tracktion::Edit::createSingleTrackEdit(engine)};

    [[nodiscard]] tracktion::AudioTrack& track() const
    {
        return *tracktion::getAudioTracks(*edit).getFirst();
    }
};

// Inserts a built-in plugin through the same pluginList path the engine uses, captures its flushed
// state, then removes and releases it so the plugin cache no longer holds its id.
[[nodiscard]] juce::ValueTree captureAndRemovePlugin(
    tracktion::AudioTrack& track, tracktion::EditItemID& original_id_out)
{
    tracktion::Plugin::Ptr plugin =
        track.pluginList.insertPlugin(tracktion::VolumeAndPanPlugin::create(), -1);
    REQUIRE(plugin != nullptr);
    original_id_out = plugin->itemID;

    plugin->flushPluginStateToValueTree();
    juce::ValueTree captured = plugin->state.createCopy();

    plugin->deleteFromParent();
    plugin = nullptr;
    return captured;
}

// Reports whether any plugin currently on the track carries the given id.
[[nodiscard]] bool trackHasPluginWithId(
    const tracktion::AudioTrack& track, tracktion::EditItemID id)
{
    for (const tracktion::Plugin* const plugin : track.pluginList)
    {
        if (plugin != nullptr && plugin->itemID == id)
        {
            return true;
        }
    }
    return false;
}

} // namespace

// Re-inserting a captured plugin state with its id intact re-creates the plugin under its original
// instance id, so the editor undo stack never needs to remap ids across a recreate.
TEST_CASE("Plugin recreate preserves the original instance id", "[audio][id-preservation]")
{
    const PluginIdPreservationHarness harness;
    tracktion::AudioTrack& track = harness.track();

    tracktion::EditItemID original_id;
    const juce::ValueTree captured = captureAndRemovePlugin(track, original_id);
    REQUIRE_FALSE(trackHasPluginWithId(track, original_id));

    const tracktion::Plugin::Ptr restored =
        track.pluginList.insertPlugin(captured.createCopy(), -1);
    REQUIRE(restored != nullptr);

    CHECK(restored->itemID == original_id);
    CHECK(trackHasPluginWithId(track, original_id));
}

} // namespace rock_hero::common::audio
