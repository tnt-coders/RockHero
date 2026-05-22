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
    }

    return {};
}

// Central source of busy-overlay presentation policy. Plugin load and audio-device open both use
// a static blocking presentation because the JUCE call that does the work
// (Tracktion plugin instantiation, juce::AudioDeviceManager::setAudioDeviceSetup) occupies the
// message thread; an animated bar would freeze and misrepresent progress.
BusyPresentation busyPresentation(BusyOperation operation) noexcept
{
    switch (operation)
    {
        case BusyOperation::LoadingPlugin:
        case BusyOperation::LoadingLiveRig:
        case BusyOperation::OpeningAudioDevice:
            return BusyPresentation::Blocking;
        case BusyOperation::OpeningProject:
        case BusyOperation::ImportingProject:
        case BusyOperation::SavingProject:
        case BusyOperation::SavingProjectAs:
        case BusyOperation::PublishingProject:
        case BusyOperation::ScanningPlugins:
            return BusyPresentation::Animated;
    }

    return BusyPresentation::Animated;
}

} // namespace rock_hero::editor::core
