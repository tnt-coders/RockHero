#include "input_calibration/input_calibration_controller.h"

#include <algorithm>
#include <cstddef>
#include <optional>
#include <rock_hero/common/audio/input/input_calibration.h>
#include <rock_hero/common/audio/input/known_interfaces.h>
#include <rock_hero/editor/core/input_calibration/input_calibration_text.h>
#include <string>
#include <utility>
#include <variant>

namespace rock_hero::editor::core
{

namespace
{

// Applies the preview gain to raw meter samples so the popup reflects the candidate calibration.
[[nodiscard]] common::audio::AudioMeterLevel applyDisplayGain(
    common::audio::AudioMeterLevel level, double gain_db)
{
    if (level.peak_db <= common::audio::minimumAudioMeterDb())
    {
        return level;
    }

    level.peak_db = std::clamp(level.peak_db + gain_db, common::audio::minimumAudioMeterDb(), 12.0);
    level.clipping = level.clipping || level.peak_db >= common::audio::clippingAudioMeterDb();
    return level;
}

// Status text for an idle popup: what to do next, and for an uncalibrated input what stays off
// until then. Both paths are named, so a player with no listed interface finds the second.
[[nodiscard]] std::string inputCalibrationIdleText(bool calibrated)
{
    return calibrated ? "Calibrated. Choose an audio device or change the gain to recalibrate."
                      : "Live input stays off until you calibrate. Choose your audio device, or "
                        "calibrate by playing.";
}

// Status text after the gain is changed by hand: the one thing left to do.
[[nodiscard]] std::string inputCalibrationEditedText()
{
    return "Click Apply to save this gain.";
}

// Status text for a chosen interface: how far to trust its figure and the setting it holds for.
// The model and the gain are left out; the chooser and the slider already show them.
[[nodiscard]] std::string interfaceSelectedText(const common::audio::KnownInterface& row)
{
    return std::string{common::audio::knownInterfaceBasisText(row.basis)} +
           " Set the audio device to " + std::string{row.unity_input} + ", then click Apply.";
}

// Status text while the measuring section is open: the setup, read before Start because the capture
// then waits only ten seconds for the first strum. Pedals lead, as the costliest mistake.
[[nodiscard]] std::string inputCalibrationSetupText()
{
    return "No pedals. Volume and tone all the way up, one pickup selected, then click Start "
           "Calibration.";
}

// Status text shown while the capture waits for the player to start: the playing the measurement
// assumes, the setup having been read when the section opened.
[[nodiscard]] std::string inputCalibrationWaitingText()
{
    return "Play as hard as you play in a song, on all strings.";
}

// Status text shown while the capture listens, counting down the whole seconds it has left.
[[nodiscard]] std::string inputCalibrationMeasuringText(std::size_t windows_remaining)
{
    const auto windows_per_second =
        static_cast<std::size_t>(common::audio::inputCalibrationSampleRateHz());
    const std::size_t seconds_left =
        (windows_remaining + windows_per_second - 1) / windows_per_second;
    return "Keep playing that hard. " + std::to_string(seconds_left) + " s left.";
}

// Status text for a saved calibration, one shape for every path: the source is named when there
// is one, so a measured figure does not read as authoritative as a published one.
[[nodiscard]] std::string savedText(double gain_db, const std::string& source)
{
    return "Saved: " + signedGainText(gain_db) + " dB" + source + ".";
}

// Status text shown when docs are unavailable from both install and build-tree locations.
[[nodiscard]] std::string inputCalibrationDocumentationUnavailableText()
{
    return "The calibration guide is not installed.";
}

} // namespace

// Seeds popup state from the prompt the editor controller projects.
InputCalibrationController::InputCalibrationController(
    Host& host, const InputCalibrationPrompt& prompt)
    : m_host(host)
    , m_committed_input_gain_db(prompt.stored_gain_db)
{
    setRestingStatus(inputCalibrationIdleText(m_committed_input_gain_db.has_value()));
    setDisplayedInputGain(m_committed_input_gain_db.value_or(common::audio::defaultGainDb()));
}

// Attaches a view after construction or replacement and synchronizes it immediately.
void InputCalibrationController::attachView(IInputCalibrationView& view)
{
    m_view = &view;
    publishState();
}

// Clears the raw view pointer only when the currently attached view is leaving.
void InputCalibrationController::detachView(IInputCalibrationView& view) noexcept
{
    if (m_view == &view)
    {
        m_view = nullptr;
    }
}

// Updates the preview gain while no measurement holds the route.
void InputCalibrationController::onManualGainChanged(double gain_db)
{
    if (m_state.measuring)
    {
        return;
    }

    setDisplayedInputGain(gain_db);
    m_state.selected_interface.reset();
    setRestingStatus(inputCalibrationEditedText());
    publishState();
}

// Opens or closes the measuring section; the status follows, giving the setup while it is open.
void InputCalibrationController::onMeasurementSectionToggled(bool open)
{
    if (m_state.measuring)
    {
        return;
    }

    m_state.measurement_section_open = open;
    // Closing returns to what the status said before the section's own texts took over.
    m_state.status_message = open ? inputCalibrationSetupText() : m_resting_status;
    publishState();
}

// Chooses the pickups the next measurement assumes; a running measurement keeps the ones it began
// with.
void InputCalibrationController::onPickupsSelected(common::audio::PickupClass pickups)
{
    if (m_state.measuring)
    {
        return;
    }

    m_state.pickups = pickups;
    publishState();
}

// Fills the slider with a known interface's derived gain; Apply then commits it as any gain.
void InputCalibrationController::onInterfaceSelected(std::size_t index)
{
    const auto rows = common::audio::knownInterfaces();
    if (m_state.measuring || index >= rows.size())
    {
        return;
    }

    const common::audio::KnownInterface& row = rows[index];
    setDisplayedInputGain(common::audio::knownInterfaceGain(row).db);
    m_state.selected_interface = index;
    setRestingStatus(interfaceSelectedText(row));
    publishState();
}

// Applies the currently displayed manual gain through the editor-runtime host.
void InputCalibrationController::onManualApplyRequested()
{
    if (m_state.measuring)
    {
        return;
    }

    const auto applied = m_host.applyManualInputCalibration(m_state.input_gain_db);
    if (!applied.has_value())
    {
        setRestingStatus(applied.error().message);
        publishState();
        return;
    }

    const std::optional<std::size_t> selected = m_state.selected_interface;
    setRestingStatus(savedText(
        m_state.input_gain_db,
        selected.has_value()
            ? " for the " + std::string{common::audio::knownInterfaces()[*selected].model}
            : std::string{}));
    m_committed_input_gain_db = m_state.input_gain_db;
    publishState();
}

// Starts an automatic measurement once the host has handed over the route.
void InputCalibrationController::onMeasurementStartRequested()
{
    if (m_state.measuring)
    {
        return;
    }

    const auto started = m_host.startInputCalibrationMeasurement(m_state.pickups);
    if (!started.has_value())
    {
        setRestingStatus(started.error().message);
        publishState();
        return;
    }

    setDisplayedInputGain(common::audio::defaultGainDb());
    m_state.selected_interface.reset();
    m_state.measuring = true;
    m_state.status_message = inputCalibrationWaitingText();
    publishState();
}

// Shows the raw input through the previewed gain, and follows a measurement in progress.
void InputCalibrationController::onSampleTick()
{
    const common::audio::LiveInputSample sample = m_host.sampleInputCalibration();
    m_last_raw_meter_level = sample.raw_level;
    m_state.input_meter_level = applyDisplayGain(sample.raw_level, m_state.input_gain_db);
    // While measuring the monitor reports every end, so a sample without progress only meters.
    if (!m_state.measuring || !sample.measurement.has_value())
    {
        publishState();
        return;
    }

    const common::audio::InputCalibrationProgress& progress = *sample.measurement;
    if (const auto* const stage =
            std::get_if<common::audio::InputCalibrationStageProgress>(&progress))
    {
        m_state.status_message = stage->stage == common::audio::InputCalibrationStage::Measuring
                                     ? inputCalibrationMeasuringText(stage->windows_remaining)
                                     : inputCalibrationWaitingText();
        publishState();
        return;
    }
    if (const auto* const committed =
            std::get_if<common::audio::InputCalibrationCommitted>(&progress))
    {
        finishMeasurementSuccess(committed->gain.db);
        return;
    }
    finishMeasurementError(std::get<common::audio::InputCalibrationFailed>(progress).message);
}

// Reports a failed help request without coupling the core controller to filesystem lookup.
void InputCalibrationController::onDocumentationUnavailable()
{
    setRestingStatus(inputCalibrationDocumentationUnavailableText());
    publishState();
}

// Forwards dismissal to the host, which ends any measurement in progress.
void InputCalibrationController::onDismissRequested()
{
    m_host.dismissInputCalibration();
}

// Updates the gain preview and recomputes the display meter from the last sampled raw level.
void InputCalibrationController::setDisplayedInputGain(double gain_db)
{
    m_state.input_gain_db = common::audio::quantizeInputCalibrationGainDb(gain_db);
    m_state.input_meter_level = applyDisplayGain(m_last_raw_meter_level, m_state.input_gain_db);
}

// Moves the popup into its completed state once the monitor has stored the measured gain.
void InputCalibrationController::finishMeasurementSuccess(double gain_db)
{
    setDisplayedInputGain(gain_db);
    // The pickups are named so a player who left the default sees what was assumed.
    setRestingStatus(savedText(
        m_state.input_gain_db,
        ", measured with " + std::string{common::audio::pickupClassText(m_state.pickups)} +
            " pickups"));
    m_committed_input_gain_db = m_state.input_gain_db;
    m_state.measuring = false;
    publishState();
}

// Returns the popup to the last committed gain; the monitor already handed the route back.
void InputCalibrationController::finishMeasurementError(std::string message)
{
    setDisplayedInputGain(m_committed_input_gain_db.value_or(common::audio::defaultGainDb()));
    setRestingStatus(std::move(message));
    m_state.measuring = false;
    publishState();
}

// Sets the status every action leaves behind; closing the measuring section returns to it.
void InputCalibrationController::setRestingStatus(std::string text)
{
    m_resting_status = text;
    m_state.status_message = std::move(text);
}

// Pushes the cached state to the attached view if one is present. The dismissal words follow the
// committed gain alone: an uncalibrated input is put off until later, a calibrated one closed.
void InputCalibrationController::publishState()
{
    m_state.dismiss_button_text = m_committed_input_gain_db.has_value() ? "Close" : "Later";
    if (m_view != nullptr)
    {
        m_view->setState(m_state);
    }
}

} // namespace rock_hero::editor::core
