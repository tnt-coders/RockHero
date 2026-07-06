#include "tracktion/multi_tone_rack.h"

#include <algorithm>
#include <cstddef>
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

// Removes source -> dest on both stereo pins.
[[nodiscard]] bool removeStereoConnection(
    tracktion::RackType& rack, tracktion::EditItemID source, tracktion::EditItemID dest)
{
    return rack.removeConnection(source, g_left_pin, dest, g_left_pin) &&
           rack.removeConnection(source, g_right_pin, dest, g_right_pin);
}

// Endpoint feeding chain position `chain_index`: the previous chain plugin, or the rack input.
[[nodiscard]] tracktion::EditItemID upstreamOfChainIndex(
    const ToneRackBranch& branch, std::size_t chain_index)
{
    return chain_index == 0 ? tracktion::EditItemID{} : branch.chain[chain_index - 1]->itemID;
}

// Endpoint fed by chain position `chain_index`: the next chain plugin, or the branch gain.
[[nodiscard]] tracktion::EditItemID downstreamOfChainIndex(
    const ToneRackBranch& branch, std::size_t chain_index)
{
    return chain_index < branch.chain.size() ? branch.chain[chain_index]->itemID
                                             : branch.branch_gain->itemID;
}

// Validates the branch/chain coordinates shared by the branch mutation entry points.
[[nodiscard]] std::expected<void, LiveRigError> validateBranchPosition(
    const ToneRack& rack, std::size_t branch_index, std::size_t chain_index,
    std::size_t chain_index_limit_offset)
{
    if (rack.rack_type == nullptr || branch_index >= rack.branches.size() ||
        chain_index >= rack.branches[branch_index].chain.size() + chain_index_limit_offset ||
        rack.branches[branch_index].branch_gain == nullptr)
    {
        return std::unexpected{LiveRigError{
            LiveRigErrorCode::InvalidRequest,
            "Multi-tone rack branch position is out of range",
        }};
    }
    return {};
}

// Rewires one branch slot from a direct upstream->downstream hop to upstream->plugin->downstream.
[[nodiscard]] bool wirePluginIntoSlot(
    tracktion::RackType& rack, const ToneRackBranch& branch, std::size_t chain_index,
    tracktion::EditItemID plugin_id)
{
    const tracktion::EditItemID upstream = upstreamOfChainIndex(branch, chain_index);
    const tracktion::EditItemID downstream = downstreamOfChainIndex(branch, chain_index);
    return removeStereoConnection(rack, upstream, downstream) &&
           addStereoConnection(rack, upstream, plugin_id) &&
           addStereoConnection(rack, plugin_id, downstream);
}

// Rewires one branch slot from upstream->plugin->downstream back to a direct hop, leaving the
// plugin unconnected.
[[nodiscard]] bool unwirePluginFromSlot(
    tracktion::RackType& rack, const ToneRackBranch& branch, std::size_t chain_index)
{
    const tracktion::EditItemID plugin_id = branch.chain[chain_index]->itemID;
    const tracktion::EditItemID upstream = upstreamOfChainIndex(branch, chain_index);
    const tracktion::EditItemID downstream = downstreamOfChainIndex(branch, chain_index + 1);
    return removeStereoConnection(rack, upstream, plugin_id) &&
           removeStereoConnection(rack, plugin_id, downstream) &&
           addStereoConnection(rack, upstream, downstream);
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

std::expected<tracktion::Plugin::Ptr, LiveRigError> createToneRackInstance(
    tracktion::Edit& edit, tracktion::RackType& rack_type)
{
    const tracktion::Plugin::Ptr instance =
        edit.getPluginCache().createNewPlugin(tracktion::RackInstance::create(rack_type));
    if (instance == nullptr || dynamic_cast<tracktion::RackInstance*>(instance.get()) == nullptr)
    {
        return std::unexpected{LiveRigError{
            LiveRigErrorCode::PluginRestoreFailed,
            "Could not create the multi-tone rack instance",
        }};
    }
    return instance;
}

bool setAudibleBranch(const ToneRack& rack, const std::string& tone_document_ref)
{
    const bool has_match =
        std::ranges::any_of(rack.branches, [&tone_document_ref](const ToneRackBranch& branch) {
            return branch.tone_document_ref == tone_document_ref;
        });
    if (!has_match)
    {
        return false;
    }

    for (const ToneRackBranch& branch : rack.branches)
    {
        if (branch.branch_gain == nullptr)
        {
            continue;
        }
        branch.branch_gain->branchGainParameter()->setParameter(
            branch.tone_document_ref == tone_document_ref ? 1.0f : 0.0f, juce::sendNotification);
    }
    return true;
}

std::expected<void, LiveRigError> insertIntoBranch(
    ToneRack& rack, std::size_t branch_index, std::size_t chain_index,
    const tracktion::Plugin::Ptr& plugin)
{
    // chain_index may equal the chain size (append before the branch gain).
    if (const auto valid = validateBranchPosition(rack, branch_index, chain_index, 1);
        !valid.has_value())
    {
        return valid;
    }
    ToneRackBranch& branch = rack.branches[branch_index];

    if (plugin == nullptr ||
        !rack.rack_type->addPlugin(
            plugin, branchPluginPosition(branch_index, chain_index, rack.branches.size()), false))
    {
        return std::unexpected{LiveRigError{
            LiveRigErrorCode::PluginRestoreFailed,
            "Could not place the plugin into the multi-tone rack branch",
        }};
    }

    if (!wirePluginIntoSlot(*rack.rack_type, branch, chain_index, plugin->itemID))
    {
        return std::unexpected{LiveRigError{
            LiveRigErrorCode::PluginRestoreFailed,
            "Could not wire the plugin into the multi-tone rack branch",
        }};
    }

    branch.chain.insert(branch.chain.begin() + static_cast<std::ptrdiff_t>(chain_index), plugin);
    return {};
}

std::expected<void, LiveRigError> removeFromBranch(
    ToneRack& rack, std::size_t branch_index, std::size_t chain_index)
{
    if (const auto valid = validateBranchPosition(rack, branch_index, chain_index, 0);
        !valid.has_value())
    {
        return valid;
    }
    ToneRackBranch& branch = rack.branches[branch_index];

    if (!unwirePluginFromSlot(*rack.rack_type, branch, chain_index))
    {
        return std::unexpected{LiveRigError{
            LiveRigErrorCode::PluginRestoreFailed,
            "Could not unwire the plugin from the multi-tone rack branch",
        }};
    }

    const tracktion::Plugin::Ptr removed = branch.chain[chain_index];
    branch.chain.erase(branch.chain.begin() + static_cast<std::ptrdiff_t>(chain_index));
    removed->removeFromParent();
    return {};
}

std::expected<void, LiveRigError> moveWithinBranch(
    ToneRack& rack, std::size_t branch_index, std::size_t from_index, std::size_t to_index)
{
    if (const auto valid = validateBranchPosition(rack, branch_index, from_index, 0);
        !valid.has_value())
    {
        return valid;
    }
    ToneRackBranch& branch = rack.branches[branch_index];
    if (to_index >= branch.chain.size())
    {
        return std::unexpected{LiveRigError{
            LiveRigErrorCode::InvalidRequest,
            "Multi-tone rack branch position is out of range",
        }};
    }
    if (from_index == to_index)
    {
        return {};
    }

    const tracktion::Plugin::Ptr moved = branch.chain[from_index];
    if (!unwirePluginFromSlot(*rack.rack_type, branch, from_index))
    {
        return std::unexpected{LiveRigError{
            LiveRigErrorCode::PluginRestoreFailed,
            "Could not unwire the plugin from the multi-tone rack branch",
        }};
    }
    branch.chain.erase(branch.chain.begin() + static_cast<std::ptrdiff_t>(from_index));

    if (!wirePluginIntoSlot(*rack.rack_type, branch, to_index, moved->itemID))
    {
        return std::unexpected{LiveRigError{
            LiveRigErrorCode::PluginRestoreFailed,
            "Could not rewire the plugin within the multi-tone rack branch",
        }};
    }
    branch.chain.insert(branch.chain.begin() + static_cast<std::ptrdiff_t>(to_index), moved);
    return {};
}

} // namespace rock_hero::common::audio
