#include "tracktion/engine_behaviors.h"
#include "tracktion/multi_tone_rack.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <memory>
#include <vector>

namespace rock_hero::common::audio
{

namespace
{

// Owns the Tracktion objects needed to build racks with Rock Hero's private plugin types.
struct MultiToneRackHarness
{
    juce::ScopedJuceInitialiser_GUI gui;
    tracktion::Engine engine{
        "RockHeroMultiToneRackTest",
        nullptr,
        std::make_unique<RockHeroEngineBehavior>(),
    };
    std::unique_ptr<tracktion::Edit> edit{tracktion::Edit::createSingleTrackEdit(engine)};
};

// Creates an engine-internal stereo plugin standing in for a VST chain entry.
[[nodiscard]] tracktion::Plugin::Ptr createChainStandIn(tracktion::Edit& edit)
{
    return edit.getPluginCache().createNewPlugin(tracktion::VolumeAndPanPlugin::xmlTypeName, {});
}

// Counts rack connections matching one endpoint pair on one pin.
[[nodiscard]] int countConnections(
    const tracktion::RackType& rack, tracktion::EditItemID source, int source_pin,
    tracktion::EditItemID dest, int dest_pin)
{
    int count = 0;
    for (const tracktion::RackConnection* const connection : rack.getConnections())
    {
        if (connection != nullptr && connection->sourceID.get() == source &&
            connection->destID.get() == dest && connection->sourcePin.get() == source_pin &&
            connection->destPin.get() == dest_pin)
        {
            ++count;
        }
    }
    return count;
}

// Checks both stereo pins of one hop exist exactly once.
[[nodiscard]] bool hasStereoConnection(
    const tracktion::RackType& rack, tracktion::EditItemID source, tracktion::EditItemID dest)
{
    return countConnections(rack, source, 1, dest, 1) == 1 &&
           countConnections(rack, source, 2, dest, 2) == 1;
}

} // namespace

// Verifies chains and passthrough branches wire input -> chain -> gain -> output per branch.
TEST_CASE("Multi-tone rack wires one branch per tone", "[audio][multi-tone-rack]")
{
    const MultiToneRackHarness harness;
    tracktion::Edit& edit = *harness.edit;

    std::vector<ToneRackBranchRequest> requests;
    requests.push_back(
        ToneRackBranchRequest{
            .tone_document_ref = "tones/aaaaaaaa-1111-4111-8111-111111111111/tone.json",
            .chain = {createChainStandIn(edit), createChainStandIn(edit)},
        });
    requests.push_back(
        ToneRackBranchRequest{
            .tone_document_ref = "tones/bbbbbbbb-2222-4222-8222-222222222222/tone.json",
            .chain = {},
        });

    const auto built = buildToneRack(edit, requests);
    REQUIRE(built.has_value());
    REQUIRE(built->rack_type != nullptr);
    REQUIRE(built->branches.size() == 2);

    // Two chain stand-ins plus two branch gains live in the rack.
    CHECK(built->rack_type->getPlugins().size() == 4);

    const ToneRackBranch& chain_branch = built->branches[0];
    REQUIRE(chain_branch.chain.size() == 2);
    REQUIRE(chain_branch.branch_gain != nullptr);
    CHECK(chain_branch.tone_document_ref == requests[0].tone_document_ref);
    CHECK(hasStereoConnection(*built->rack_type, {}, chain_branch.chain[0]->itemID));
    CHECK(hasStereoConnection(
        *built->rack_type, chain_branch.chain[0]->itemID, chain_branch.chain[1]->itemID));
    CHECK(hasStereoConnection(
        *built->rack_type, chain_branch.chain[1]->itemID, chain_branch.branch_gain->itemID));
    CHECK(hasStereoConnection(*built->rack_type, chain_branch.branch_gain->itemID, {}));

    const ToneRackBranch& passthrough_branch = built->branches[1];
    REQUIRE(passthrough_branch.chain.empty());
    REQUIRE(passthrough_branch.branch_gain != nullptr);
    CHECK(hasStereoConnection(*built->rack_type, {}, passthrough_branch.branch_gain->itemID));
    CHECK(hasStereoConnection(*built->rack_type, passthrough_branch.branch_gain->itemID, {}));

    // Every hop is stereo: (2 chain hops + gain hop + output hop) + (gain hop + output hop).
    CHECK(built->rack_type->getConnections().size() == 12);
}

// Verifies branch gains start audible and adjust independently per tone.
TEST_CASE("Multi-tone rack branch gains are independent", "[audio][multi-tone-rack]")
{
    const MultiToneRackHarness harness;
    tracktion::Edit& edit = *harness.edit;

    const std::vector<ToneRackBranchRequest> requests{
        ToneRackBranchRequest{
            .tone_document_ref = "tones/aaaaaaaa-1111-4111-8111-111111111111/tone.json",
            .chain = {},
        },
        ToneRackBranchRequest{
            .tone_document_ref = "tones/bbbbbbbb-2222-4222-8222-222222222222/tone.json",
            .chain = {},
        },
    };

    const auto built = buildToneRack(edit, requests);
    REQUIRE(built.has_value());
    REQUIRE(built->branches.size() == 2);

    const tracktion::AutomatableParameter::Ptr first_gain =
        built->branches[0].branch_gain->branchGainParameter();
    const tracktion::AutomatableParameter::Ptr second_gain =
        built->branches[1].branch_gain->branchGainParameter();
    REQUIRE(first_gain != nullptr);
    REQUIRE(second_gain != nullptr);
    CHECK(first_gain->getCurrentValue() == Catch::Approx(1.0f));
    CHECK(second_gain->getCurrentValue() == Catch::Approx(1.0f));

    first_gain->setParameter(0.0f, juce::dontSendNotification);
    CHECK(first_gain->getCurrentValue() == Catch::Approx(0.0f));
    CHECK(second_gain->getCurrentValue() == Catch::Approx(1.0f));
}

// Verifies audible-branch switching drives exactly one branch to full gain.
TEST_CASE("Multi-tone rack switches the audible branch", "[audio][multi-tone-rack]")
{
    const MultiToneRackHarness harness;
    tracktion::Edit& edit = *harness.edit;

    const std::vector<ToneRackBranchRequest> requests{
        ToneRackBranchRequest{
            .tone_document_ref = "tones/aaaaaaaa-1111-4111-8111-111111111111/tone.json",
            .chain = {},
        },
        ToneRackBranchRequest{
            .tone_document_ref = "tones/bbbbbbbb-2222-4222-8222-222222222222/tone.json",
            .chain = {},
        },
    };
    auto built = buildToneRack(edit, requests);
    REQUIRE(built.has_value());

    REQUIRE(setAudibleBranch(*built, requests[1].tone_document_ref));
    CHECK(
        built->branches[0].branch_gain->branchGainParameter()->getCurrentValue() ==
        Catch::Approx(0.0f));
    CHECK(
        built->branches[1].branch_gain->branchGainParameter()->getCurrentValue() ==
        Catch::Approx(1.0f));

    // Unknown tones leave the gains untouched.
    CHECK_FALSE(setAudibleBranch(*built, "tones/cccccccc-3333-4333-8333-333333333333/tone.json"));
    CHECK(
        built->branches[1].branch_gain->branchGainParameter()->getCurrentValue() ==
        Catch::Approx(1.0f));
}

// Verifies chain mutations rewire branch hops and never disturb the other branch.
TEST_CASE("Multi-tone rack inserts removes and moves within a branch", "[audio][multi-tone-rack]")
{
    const MultiToneRackHarness harness;
    tracktion::Edit& edit = *harness.edit;

    const std::vector<ToneRackBranchRequest> requests{
        ToneRackBranchRequest{
            .tone_document_ref = "tones/aaaaaaaa-1111-4111-8111-111111111111/tone.json",
            .chain = {},
        },
        ToneRackBranchRequest{
            .tone_document_ref = "tones/bbbbbbbb-2222-4222-8222-222222222222/tone.json",
            .chain = {},
        },
    };
    auto built = buildToneRack(edit, requests);
    REQUIRE(built.has_value());
    const tracktion::EditItemID gain_id = built->branches[0].branch_gain->itemID;
    const tracktion::EditItemID other_gain_id = built->branches[1].branch_gain->itemID;

    // Insert first into the empty branch, then prepend a second plugin in front of it.
    const tracktion::Plugin::Ptr second = createChainStandIn(edit);
    REQUIRE(insertIntoBranch(*built, 0, 0, second).has_value());
    const tracktion::Plugin::Ptr first = createChainStandIn(edit);
    REQUIRE(insertIntoBranch(*built, 0, 0, first).has_value());
    REQUIRE(built->branches[0].chain.size() == 2);
    CHECK(built->branches[0].chain[0] == first);
    CHECK(built->branches[0].chain[1] == second);
    CHECK(hasStereoConnection(*built->rack_type, {}, first->itemID));
    CHECK(hasStereoConnection(*built->rack_type, first->itemID, second->itemID));
    CHECK(hasStereoConnection(*built->rack_type, second->itemID, gain_id));
    CHECK_FALSE(hasStereoConnection(*built->rack_type, {}, gain_id));

    // Move the first plugin behind the second one.
    REQUIRE(moveWithinBranch(*built, 0, 0, 1).has_value());
    CHECK(built->branches[0].chain[0] == second);
    CHECK(built->branches[0].chain[1] == first);
    CHECK(hasStereoConnection(*built->rack_type, {}, second->itemID));
    CHECK(hasStereoConnection(*built->rack_type, second->itemID, first->itemID));
    CHECK(hasStereoConnection(*built->rack_type, first->itemID, gain_id));

    // Remove the now-first plugin; the survivor bridges input to gain around it.
    REQUIRE(removeFromBranch(*built, 0, 0).has_value());
    REQUIRE(built->branches[0].chain.size() == 1);
    CHECK(built->branches[0].chain[0] == first);
    CHECK(hasStereoConnection(*built->rack_type, {}, first->itemID));
    CHECK(hasStereoConnection(*built->rack_type, first->itemID, gain_id));
    CHECK(built->rack_type->getPluginForID(second->itemID) == nullptr);

    // The untouched branch still runs input -> gain -> output.
    CHECK(hasStereoConnection(*built->rack_type, {}, other_gain_id));
    CHECK(hasStereoConnection(*built->rack_type, other_gain_id, {}));

    // Out-of-range coordinates are rejected without changing the chain.
    CHECK_FALSE(removeFromBranch(*built, 0, 5).has_value());
    CHECK_FALSE(insertIntoBranch(*built, 9, 0, createChainStandIn(edit)).has_value());
    CHECK(built->branches[0].chain.size() == 1);
}

// Verifies the rack instance plugin is created ready for track placement.
TEST_CASE("Multi-tone rack creates a track instance", "[audio][multi-tone-rack]")
{
    const MultiToneRackHarness harness;
    tracktion::Edit& edit = *harness.edit;

    const std::vector<ToneRackBranchRequest> requests{
        ToneRackBranchRequest{
            .tone_document_ref = "tones/aaaaaaaa-1111-4111-8111-111111111111/tone.json",
            .chain = {},
        },
    };
    const auto built = buildToneRack(edit, requests);
    REQUIRE(built.has_value());

    const auto instance = createToneRackInstance(edit, *built->rack_type);
    REQUIRE(instance.has_value());
    auto* const rack_instance = dynamic_cast<tracktion::RackInstance*>(instance->get());
    REQUIRE(rack_instance != nullptr);
    CHECK(rack_instance->type == built->rack_type);
}

} // namespace rock_hero::common::audio
