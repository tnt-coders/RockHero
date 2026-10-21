#include <rock_hero/editor/core/timeline/marker_verb.h>
#include <string>

namespace rock_hero::editor::core
{

// Names each marker verb once: its undo entry, refusal records, prompt title and direct menu row.
std::string markerVerbLabel(const MarkerVerb verb)
{
    switch (verb)
    {
        case MarkerVerb::AddSection:
        {
            return "Add Section";
        }
        case MarkerVerb::RenameSection:
        {
            return "Rename Section";
        }
        case MarkerVerb::MoveSection:
        {
            return "Move Section";
        }
        case MarkerVerb::AddTempoAnchor:
        {
            return "Add Tempo Anchor";
        }
        case MarkerVerb::SetTempo:
        {
            return "Set Tempo";
        }
        case MarkerVerb::MoveTempoAnchor:
        {
            return "Move Tempo Anchor";
        }
        case MarkerVerb::DeleteTempoAnchor:
        {
            return "Delete Tempo Anchor";
        }
        case MarkerVerb::ScaleTempo:
        {
            return "Scale Tempo";
        }
        case MarkerVerb::AddTimeSignature:
        {
            return "Add Time Signature";
        }
        case MarkerVerb::SetTimeSignature:
        {
            return "Set Time Signature";
        }
        case MarkerVerb::MoveTimeSignature:
        {
            return "Move Time Signature";
        }
        case MarkerVerb::DeleteTimeSignature:
        {
            return "Delete Time Signature";
        }
        case MarkerVerb::InsertToneChange:
        {
            return "Insert Tone Change";
        }
        case MarkerVerb::AddHandPosition:
        {
            return "Add Hand Position";
        }
        case MarkerVerb::MoveHandPosition:
        {
            return "Move Hand Position";
        }
        case MarkerVerb::SetHandPositionStartFret:
        {
            return "Set Hand Position Start Fret";
        }
        case MarkerVerb::SetHandPositionEndFret:
        {
            return "Set Hand Position End Fret";
        }
        case MarkerVerb::ClearHandPositionEndFret:
        {
            return "Clear Hand Position End Fret";
        }
        case MarkerVerb::DeleteHandPosition:
        {
            return "Delete Hand Position";
        }
        case MarkerVerb::PruneHandPositions:
        {
            return "Prune Hand Positions";
        }
        case MarkerVerb::ChooseHold:
        {
            return "Hold";
        }
        case MarkerVerb::HoldShape:
        {
            return "Hold Shape";
        }
        case MarkerVerb::MoveHoldStart:
        {
            return "Move Hold Start";
        }
        case MarkerVerb::MoveHoldEnd:
        {
            return "Move Hold End";
        }
        case MarkerVerb::DeleteHold:
        {
            return "Delete Hold";
        }
        case MarkerVerb::HoldNone:
        {
            return "Hold None";
        }
        case MarkerVerb::HoldAuto:
        {
            return "Hold Auto";
        }
    }
    return {};
}

// The verb's label with the ellipsis a menu row carries when it opens a prompt.
std::string markerVerbPromptRow(const MarkerVerb verb)
{
    return markerVerbLabel(verb) + "...";
}

// The Scale Tempo label, saying whether the Note Lock keeps notes at their times or lets them
// follow the beat.
std::string scaleTempoLabel(const bool note_lock)
{
    return markerVerbLabel(MarkerVerb::ScaleTempo) +
           (note_lock ? " (notes keep their time)" : " (notes follow the beat)");
}

} // namespace rock_hero::editor::core
