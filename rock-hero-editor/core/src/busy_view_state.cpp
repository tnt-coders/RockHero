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
        case BusyOperation::ChangingAudioDevice:
            return "Changing audio device...";
        case BusyOperation::LoadingPlugin:
            return "Loading plugin...";
    }

    return {};
}

// Central source of busy-overlay presentation policy. Plugin load uses a static blocking
// presentation because Tracktion/JUCE instantiation occupies the message thread.
BusyPresentation busyPresentation(BusyOperation operation) noexcept
{
    switch (operation)
    {
        case BusyOperation::LoadingPlugin:
            return BusyPresentation::Blocking;
        case BusyOperation::OpeningProject:
        case BusyOperation::ImportingProject:
        case BusyOperation::SavingProject:
        case BusyOperation::SavingProjectAs:
        case BusyOperation::PublishingProject:
        case BusyOperation::ChangingAudioDevice:
            return BusyPresentation::Animated;
    }

    return BusyPresentation::Animated;
}

} // namespace rock_hero::editor::core
