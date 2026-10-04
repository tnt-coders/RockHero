#include <catch2/catch_test_macros.hpp>
#include <rock_hero/editor/core/timeline/marker_verb.h>

namespace rock_hero::editor::core
{

// The table is the one spelling of every marker verb's label, read by its undo entry, its refusal
// log, its prompt's title, its menu rows and its registered command, so each label is pinned here
// byte for byte: a change to any of them is a user-visible change and must fail a test.
TEST_CASE("Every marker verb's label is pinned", "[core][marker-verb]")
{
    CHECK(markerVerbLabel(MarkerVerb::AddSection) == "Add Section");
    CHECK(markerVerbLabel(MarkerVerb::RenameSection) == "Rename Section");
    CHECK(markerVerbLabel(MarkerVerb::MoveSection) == "Move Section");
    CHECK(markerVerbLabel(MarkerVerb::AddTempoAnchor) == "Add Tempo Anchor");
    CHECK(markerVerbLabel(MarkerVerb::SetTempo) == "Set Tempo");
    CHECK(markerVerbLabel(MarkerVerb::MoveTempoAnchor) == "Move Tempo Anchor");
    CHECK(markerVerbLabel(MarkerVerb::DeleteTempoAnchor) == "Delete Tempo Anchor");
    CHECK(markerVerbLabel(MarkerVerb::ScaleTempo) == "Scale Tempo");
    CHECK(markerVerbLabel(MarkerVerb::AddTimeSignature) == "Add Time Signature");
    CHECK(markerVerbLabel(MarkerVerb::SetTimeSignature) == "Set Time Signature");
    CHECK(markerVerbLabel(MarkerVerb::MoveTimeSignature) == "Move Time Signature");
    CHECK(markerVerbLabel(MarkerVerb::DeleteTimeSignature) == "Delete Time Signature");
    CHECK(markerVerbLabel(MarkerVerb::AddHandPosition) == "Add Hand Position");
    CHECK(markerVerbLabel(MarkerVerb::MoveHandPosition) == "Move Hand Position");
    CHECK(markerVerbLabel(MarkerVerb::SetHandPositionStartFret) == "Set Hand Position Start Fret");
    CHECK(markerVerbLabel(MarkerVerb::SetHandPositionEndFret) == "Set Hand Position End Fret");
    CHECK(markerVerbLabel(MarkerVerb::ClearHandPositionEndFret) == "Clear Hand Position End Fret");
    CHECK(markerVerbLabel(MarkerVerb::DeleteHandPosition) == "Delete Hand Position");
}

// The ellipsis is the one display convention for a row that opens the verb's prompt, applied to
// the label rather than spelled a second time.
TEST_CASE("A prompt row is its verb's label and an ellipsis", "[core][marker-verb]")
{
    CHECK(markerVerbPromptRow(MarkerVerb::SetTempo) == "Set Tempo...");
    CHECK(markerVerbPromptRow(MarkerVerb::SetTimeSignature) == "Set Time Signature...");
    CHECK(markerVerbPromptRow(MarkerVerb::ScaleTempo) == "Scale Tempo...");
}

} // namespace rock_hero::editor::core
