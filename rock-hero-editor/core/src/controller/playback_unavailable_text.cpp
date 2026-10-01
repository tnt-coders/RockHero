#include "playback_unavailable_text.h"

namespace rock_hero::editor::core
{

std::string playbackUnavailableText(ActionUnavailableReason reason)
{
    switch (reason)
    {
        case ActionUnavailableReason::Busy:
        {
            return "Playback disabled: busy.";
        }
        case ActionUnavailableReason::SessionFaulted:
        {
            return "Playback disabled: live editor state untrusted.";
        }
        case ActionUnavailableReason::InputCalibrationPrompt:
        {
            return "Playback disabled: input calibration in progress.";
        }
        case ActionUnavailableReason::NoLoadedArrangement:
        {
            return "Playback disabled: no song open.";
        }
        case ActionUnavailableReason::AudioDeviceClosed:
        {
            return "Playback disabled: audio device closed.";
        }
        // The transport's gate never names these; the switch lists them so a new reason is a
        // compile-time decision here.
        case ActionUnavailableReason::BusyCancelUnavailable:
        case ActionUnavailableReason::NotBusy:
        case ActionUnavailableReason::NoProject:
        case ActionUnavailableReason::NoChart:
        case ActionUnavailableReason::NoChartSelection:
        case ActionUnavailableReason::NoArmedCaret:
        case ActionUnavailableReason::NoUnsavedChangesPrompt:
        case ActionUnavailableReason::NoSaveAsPrompt:
        case ActionUnavailableReason::NoToneImportPrompt:
        case ActionUnavailableReason::HistoryUnavailable:
        case ActionUnavailableReason::TransportPlaying:
        case ActionUnavailableReason::LiveInputAuditionUnavailable:
        case ActionUnavailableReason::ToneDesignerInactive:
        case ActionUnavailableReason::PluginChainFull:
        case ActionUnavailableReason::NoPluginCandidates:
        case ActionUnavailableReason::NoLoadedPlugins:
        {
            return "Playback disabled.";
        }
    }

    return "Playback disabled.";
}

} // namespace rock_hero::editor::core
