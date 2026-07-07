/*!
\file multi_tone_rack.h
\brief Assembly of the multi-tone RackType from per-tone plugin chains.
*/

#pragma once

#include "tracktion/tone_branch_gain_plugin.h"

#include <expected>
#include <rock_hero/common/audio/live_rig/live_rig_error.h>
#include <span>
#include <string>
#include <tracktion_engine/tracktion_engine.h>
#include <vector>

namespace rock_hero::common::audio
{

/*!
\brief One tone branch to wire into the multi-tone rack.

The chain plugins must already exist through the edit's plugin cache; assembly only moves them
into the rack and wires connections. An empty chain is valid and produces a passthrough branch
(rack input straight into the branch gain).
*/
struct ToneRackBranchRequest
{
    /*! \brief Canonical tone document reference identifying this branch's tone. */
    std::string tone_document_ref;

    /*! \brief Ordered user plugin chain for this tone; may be empty. */
    std::vector<tracktion::Plugin::Ptr> chain;
};

/*!
\brief Handles to one wired tone branch inside the built rack.

The rack owns the plugins through its plugin-instance state; these handles stay valid while the
owning \ref ToneRack (and its rack type) is alive and the branch is not structurally edited.
*/
struct ToneRackBranch
{
    /*! \brief Canonical tone document reference identifying this branch's tone. */
    std::string tone_document_ref;

    /*! \brief Ordered user plugin chain for this tone; may be empty. */
    std::vector<tracktion::Plugin::Ptr> chain;

    /*! \brief Branch-terminating gain whose automation curve carries the region schedule. */
    ToneBranchGainPlugin* branch_gain{nullptr};
};

/*!
\brief The built multi-tone rack: one parallel branch per tone, summed to the rack output.
*/
struct ToneRack
{
    /*! \brief Rack type holding every branch; place one RackInstance of it on the track. */
    tracktion::RackType::Ptr rack_type;

    /*! \brief Wired branches in request order. */
    std::vector<ToneRackBranch> branches;
};

/*!
\brief Builds one rack with a parallel branch per requested tone.

Each branch runs rack stereo input through the request's plugin chain in order, then through a
new \ref ToneBranchGainPlugin, into the rack's summed stereo output. Branch gains default to
fully audible; schedule baking and selection control them afterwards.

\param edit Tracktion edit that owns the rack list and plugin cache.
\param requests Branches to wire, one per tone, in display order.
\return The built rack with branch handles, or the first wiring failure.
*/
[[nodiscard]] std::expected<ToneRack, LiveRigError> buildToneRack(
    tracktion::Edit& edit, std::span<const ToneRackBranchRequest> requests);

/*!
\brief Appends one empty (passthrough) branch for a tone to an already-built live rack.

Rack mutations are ValueTree edits that Tracktion coalesces into one asynchronous graph rebuild
which reuses every live plugin instance, so adding an empty branch — one \ref ToneBranchGainPlugin
plus its stereo wiring — never re-instantiates existing plugins or stops playback. This is the
fast path for creating a new (empty) tone; branches with persisted plugins still load through
\ref buildToneRack.

\param rack Built rack to mutate; a branch handle is appended on success.
\param edit Tracktion edit that owns the plugin cache.
\param tone_document_ref Tone the new branch represents.
\return Empty success, or a typed wiring failure.
*/
[[nodiscard]] std::expected<void, LiveRigError> addEmptyToneBranch(
    ToneRack& rack, tracktion::Edit& edit, const std::string& tone_document_ref);

/*!
\brief Creates a RackInstance plugin of the rack for placement on the instrument track.
\param edit Tracktion edit that owns the plugin cache.
\param rack_type Rack the instance should host.
\return The created rack instance plugin, or a typed failure.
*/
[[nodiscard]] std::expected<tracktion::Plugin::Ptr, LiveRigError> createToneRackInstance(
    tracktion::Edit& edit, tracktion::RackType& rack_type);

/*!
\brief Makes exactly one branch audible and silences the others.

Gains move through each branch plugin's per-sample smoother, so switching is click-free. This is
the direct selection-driven switch path; baked schedule automation replaces it during playback
once schedules land.

\param rack Built rack whose branches should switch.
\param tone_document_ref Tone whose branch becomes audible.
\return True when a branch matched the reference; false leaves gains unchanged.
*/
[[nodiscard]] bool setAudibleBranch(const ToneRack& rack, const std::string& tone_document_ref);

/*!
\brief Inserts an already-created plugin into one branch's chain, rewiring around it.
\param rack Built rack to mutate; the branch's chain handle updates on success.
\param branch_index Branch to insert into.
\param chain_index Position within the branch chain (0 to chain size inclusive).
\param plugin Plugin created through the edit's plugin cache.
\return Empty success, or a typed wiring failure.
*/
[[nodiscard]] std::expected<void, LiveRigError> insertIntoBranch(
    ToneRack& rack, std::size_t branch_index, std::size_t chain_index,
    const tracktion::Plugin::Ptr& plugin);

/*!
\brief Removes one chain plugin from a branch, bridging its neighbors.

The plugin leaves the rack entirely; the rack cleans its dangling connections.

\param rack Built rack to mutate; the branch's chain handle updates on success.
\param branch_index Branch to remove from.
\param chain_index Position of the plugin to remove.
\return Empty success, or a typed wiring failure.
*/
[[nodiscard]] std::expected<void, LiveRigError> removeFromBranch(
    ToneRack& rack, std::size_t branch_index, std::size_t chain_index);

/*!
\brief Moves one chain plugin to a new position within its branch, rewiring both sites.
\param rack Built rack to mutate; the branch's chain handle updates on success.
\param branch_index Branch to mutate.
\param from_index Current position of the plugin.
\param to_index Desired position after the move, in post-removal indexing.
\return Empty success, or a typed wiring failure.
*/
[[nodiscard]] std::expected<void, LiveRigError> moveWithinBranch(
    ToneRack& rack, std::size_t branch_index, std::size_t from_index, std::size_t to_index);

} // namespace rock_hero::common::audio
