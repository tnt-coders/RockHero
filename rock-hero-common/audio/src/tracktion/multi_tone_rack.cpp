#include "tracktion/multi_tone_rack.h"

#include <utility>

namespace rock_hero::common::audio
{

namespace
{

// Rack connections address audio as 1-based pins (pin 0 is MIDI); the rig is stereo throughout.
constexpr int g_left_pin{1};
constexpr int g_right_pin{2};

// Connects source to dest on both stereo pins; an invalid EditItemID addresses the rack itself.
[[nodiscard]] bool addStereoConnection(
    tracktion::RackType& rack, tracktion::EditItemID source, tracktion::EditItemID dest)
{
    return rack.addConnection(source, g_left_pin, dest, g_left_pin) &&
           rack.addConnection(source, g_right_pin, dest, g_right_pin);
}

// Places branch plugins on a display grid so opening the rack in diagnostics stays readable.
[[nodiscard]] juce::Point<float> branchPluginPosition(
    std::size_t branch_index, std::size_t column, std::size_t branch_count)
{
    const float y = branch_count > 1
                        ? static_cast<float>(branch_index) / static_cast<float>(branch_count - 1)
                        : 0.5f;
    return {0.1f + 0.1f * static_cast<float>(column), y};
}

} // namespace

std::expected<ToneRack, LiveRigError> buildToneRack(
    tracktion::Edit& edit, std::span<const ToneRackBranchRequest> requests)
{
    const tracktion::RackType::Ptr rack_type = edit.getRackList().addNewRack();
    if (rack_type == nullptr)
    {
        return std::unexpected{LiveRigError{
            LiveRigErrorCode::PluginRestoreFailed,
            "Could not create the multi-tone rack",
        }};
    }
    rack_type->rackName = "Rock Hero Tones";

    ToneRack tone_rack{.rack_type = rack_type, .branches = {}};
    tone_rack.branches.reserve(requests.size());

    for (std::size_t branch_index = 0; branch_index < requests.size(); ++branch_index)
    {
        const ToneRackBranchRequest& request = requests[branch_index];

        const tracktion::Plugin::Ptr gain_plugin =
            edit.getPluginCache().createNewPlugin(ToneBranchGainPlugin::createState());
        auto* const branch_gain = dynamic_cast<ToneBranchGainPlugin*>(gain_plugin.get());
        if (branch_gain == nullptr)
        {
            return std::unexpected{LiveRigError{
                LiveRigErrorCode::PluginRestoreFailed,
                "Could not create the tone branch gain for " + request.tone_document_ref,
            }};
        }

        // Chain plugins first, then the terminating gain, all without auto-connection so the
        // explicit branch wiring below stays the only topology.
        for (std::size_t column = 0; column < request.chain.size(); ++column)
        {
            if (request.chain[column] == nullptr ||
                !rack_type->addPlugin(
                    request.chain[column],
                    branchPluginPosition(branch_index, column, requests.size()),
                    false))
            {
                return std::unexpected{LiveRigError{
                    LiveRigErrorCode::PluginRestoreFailed,
                    "Could not place a chain plugin into the multi-tone rack for " +
                        request.tone_document_ref,
                }};
            }
        }
        if (!rack_type->addPlugin(
                gain_plugin,
                branchPluginPosition(branch_index, request.chain.size(), requests.size()),
                false))
        {
            return std::unexpected{LiveRigError{
                LiveRigErrorCode::PluginRestoreFailed,
                "Could not place the tone branch gain into the multi-tone rack for " +
                    request.tone_document_ref,
            }};
        }

        // Wire rack input -> chain -> branch gain -> rack output on both stereo pins. An empty
        // chain wires the rack input straight into the branch gain.
        tracktion::EditItemID upstream{};
        bool wired = true;
        for (const tracktion::Plugin::Ptr& chain_plugin : request.chain)
        {
            wired = wired && addStereoConnection(*rack_type, upstream, chain_plugin->itemID);
            upstream = chain_plugin->itemID;
        }
        wired = wired && addStereoConnection(*rack_type, upstream, branch_gain->itemID);
        wired = wired && addStereoConnection(*rack_type, branch_gain->itemID, {});
        if (!wired)
        {
            return std::unexpected{LiveRigError{
                LiveRigErrorCode::PluginRestoreFailed,
                "Could not wire the multi-tone rack branch for " + request.tone_document_ref,
            }};
        }

        tone_rack.branches.push_back(
            ToneRackBranch{
                .tone_document_ref = request.tone_document_ref,
                .chain = request.chain,
                .branch_gain = branch_gain,
            });
    }

    return tone_rack;
}

} // namespace rock_hero::common::audio
