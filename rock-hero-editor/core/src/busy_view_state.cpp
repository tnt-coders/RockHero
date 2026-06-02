#include "busy_view_state.h"

namespace rock_hero::editor::core
{

// Central source of busy-overlay default copy. Controller fills BusyViewState::message from this
// helper at beginBusy() time so every entry point produces consistent text without each call site
// retyping it.
std::string busyMessage(BusyOperation operation)
{
    switch (operation)
    {
        case BusyOperation::OpeningProject:
            return "Opening project...";
        case BusyOperation::ImportingProject:
            return "Importing project...";
        case BusyOperation::SavingProject:
        case BusyOperation::SavingProjectAs:
            return "Saving project...";
        case BusyOperation::PublishingProject:
            return "Publishing project...";
        case BusyOperation::OpeningAudioDevice:
            return "Opening audio device...";
        case BusyOperation::LoadingPlugin:
            return "Loading plugin...";
        case BusyOperation::ScanningPlugins:
            return "Scanning plugins...";
        case BusyOperation::LoadingLiveRig:
            return "Loading live rig...";
        case BusyOperation::AnalyzingBackingAudio:
            return "Analyzing audio...";
    }

    return {};
}

// Central source of busy-overlay indicator policy. Plugin load and audio-device open both use
// message-only indicators because the JUCE call that does the work occupies the message thread;
// a progress bar would freeze and misrepresent progress.
BusyIndicator busyIndicator(BusyOperation operation) noexcept
{
    switch (operation)
    {
        case BusyOperation::LoadingPlugin:
        case BusyOperation::OpeningAudioDevice:
            return BusyIndicator::MessageOnly;
        case BusyOperation::LoadingLiveRig:
            return BusyIndicator::DeterminateProgress;
        case BusyOperation::OpeningProject:
        case BusyOperation::ImportingProject:
        case BusyOperation::SavingProject:
        case BusyOperation::SavingProjectAs:
        case BusyOperation::PublishingProject:
        case BusyOperation::ScanningPlugins:
        case BusyOperation::AnalyzingBackingAudio:
            return BusyIndicator::IndeterminateProgress;
    }

    return BusyIndicator::IndeterminateProgress;
}

} // namespace rock_hero::editor::core
