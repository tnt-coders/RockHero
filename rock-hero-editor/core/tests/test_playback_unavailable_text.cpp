#include "controller/playback_unavailable_text.h"

#include <catch2/catch_test_macros.hpp>

namespace rock_hero::editor::core
{

// Each reason the transport's gate can name maps to its fixed tooltip: the condition, not its fix.
TEST_CASE("Playback text names each transport refusal", "[core][editor-action]")
{
    CHECK(
        playbackUnavailableText(ActionUnavailableReason::NoLoadedArrangement) ==
        "Playback disabled: no song open.");
    CHECK(
        playbackUnavailableText(ActionUnavailableReason::InputCalibrationPrompt) ==
        "Playback disabled: input calibration in progress.");
    CHECK(
        playbackUnavailableText(ActionUnavailableReason::SessionFaulted) ==
        "Playback disabled: live editor state untrusted.");
    CHECK(playbackUnavailableText(ActionUnavailableReason::Busy) == "Playback disabled: busy.");
}

} // namespace rock_hero::editor::core
