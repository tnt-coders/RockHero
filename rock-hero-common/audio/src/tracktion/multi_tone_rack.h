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

} // namespace rock_hero::common::audio
