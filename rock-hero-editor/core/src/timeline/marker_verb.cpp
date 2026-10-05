#include <rock_hero/editor/core/timeline/marker_verb.h>
#include <string>

namespace rock_hero::editor::core
{

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
    }
    return {};
}

std::string markerVerbPromptRow(const MarkerVerb verb)
{
    return markerVerbLabel(verb) + "...";
}

} // namespace rock_hero::editor::core
