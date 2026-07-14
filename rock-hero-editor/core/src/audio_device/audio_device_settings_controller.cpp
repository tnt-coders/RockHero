#include "audio_device/audio_device_settings_controller.h"

#include <cmath>
#include <format>
#include <memory>
#include <string>
#include <utility>

namespace rock_hero::editor::core
{

namespace
{

// Distance from an integer at which a sample rate is rendered without a fractional component.
// Distinct from common/audio's same-selection tolerance: this is a display-rounding rule, not a
// hardware-rate comparison. The two values happen to agree today but track different concerns.
constexpr double g_integer_sample_rate_display_threshold{0.001};

// Converts a label list into one-based choices consumed by JUCE ComboBox item IDs.
[[nodiscard]] std::vector<AudioDeviceSettingsViewState::Choice> choicesFromLabels(
    const std::vector<std::string>& labels)
{
    std::vector<AudioDeviceSettingsViewState::Choice> choices;
    choices.reserve(labels.size());

    for (int index = 0; std::cmp_less(index, labels.size()); ++index)
    {
        choices.push_back(
            AudioDeviceSettingsViewState::Choice{
                .id = index + 1,
                .label = labels[static_cast<std::size_t>(index)],
            });
    }

    return choices;
}

// Formats sample-rate choices as whole Hz when possible.
[[nodiscard]] std::string sampleRateLabel(double sample_rate)
{
    const auto rounded = static_cast<int>(std::lround(sample_rate));
    if (std::abs(sample_rate - static_cast<double>(rounded)) <
        g_integer_sample_rate_display_threshold)
    {
        return std::format("{} Hz", rounded);
    }

    return std::format("{:.1f} Hz", sample_rate);
}

// Converts available sample rates into one-based display choices.
[[nodiscard]] std::vector<AudioDeviceSettingsViewState::Choice> sampleRateChoices(
    const std::vector<double>& sample_rates)
{
    std::vector<AudioDeviceSettingsViewState::Choice> choices;
    choices.reserve(sample_rates.size());

    for (int index = 0; std::cmp_less(index, sample_rates.size()); ++index)
    {
        choices.push_back(
            AudioDeviceSettingsViewState::Choice{
                .id = index + 1,
                .label = sampleRateLabel(sample_rates[static_cast<std::size_t>(index)]),
            });
    }

    return choices;
}

// Converts buffer sizes into one-based display choices.
[[nodiscard]] std::vector<AudioDeviceSettingsViewState::Choice> bufferSizeChoices(
    const std::vector<int>& buffer_sizes)
{
    std::vector<AudioDeviceSettingsViewState::Choice> choices;
    choices.reserve(buffer_sizes.size());

    for (int index = 0; std::cmp_less(index, buffer_sizes.size()); ++index)
    {
        choices.push_back(
            AudioDeviceSettingsViewState::Choice{
                .id = index + 1,
                .label = std::to_string(buffer_sizes[static_cast<std::size_t>(index)]) + " samples",
            });
    }

    return choices;
}

// Converts common/audio state into the editor view model consumed by the settings view.
[[nodiscard]] AudioDeviceSettingsViewState toViewState(
    const common::audio::AudioDeviceSettingsState& state)
{
    std::vector<std::string> stereo_pair_labels;
    stereo_pair_labels.reserve(state.stereo_output_pairs.size());
    for (const auto& pair : state.stereo_output_pairs)
    {
        stereo_pair_labels.push_back(pair.label);
    }

    return AudioDeviceSettingsViewState{
        .audio_systems = choicesFromLabels(state.audio_systems),
        .selected_audio_system_id = state.selected_audio_system_id,
        .uses_separate_input_output_devices = state.uses_separate_input_output_devices,
        .devices = choicesFromLabels(state.devices),
        .selected_device_id = state.selected_device_id,
        .input_devices = choicesFromLabels(state.input_devices),
        .selected_input_device_id = state.selected_input_device_id,
        .output_devices = choicesFromLabels(state.output_devices),
        .selected_output_device_id = state.selected_output_device_id,
        .input_channels = choicesFromLabels(state.input_channels),
        .selected_input_channel_id = state.selected_input_channel_id,
        .stereo_output_pairs = choicesFromLabels(stereo_pair_labels),
        .selected_stereo_output_pair_id = state.selected_stereo_output_pair_id,
        .sample_rates = sampleRateChoices(state.sample_rates),
        .selected_sample_rate_id = state.selected_sample_rate_id,
        .buffer_sizes = bufferSizeChoices(state.buffer_sizes),
        .selected_buffer_size_id = state.selected_buffer_size_id,
        .control_panel_supported = state.control_panel_supported,
        .staged_device_error = state.staged_device_error,
        .ok_enabled =
            state.selected_audio_system_id > 0 &&
            (state.uses_separate_input_output_devices
                 ? state.selected_input_device_id > 0 && state.selected_output_device_id > 0
                 : state.selected_device_id > 0),
        .error_message = state.error_message,
    };
}

} // namespace

// Subscribes for backend refreshes while the already-started settings edit is alive.
AudioDeviceSettingsController::AudioDeviceSettingsController(
    common::audio::IAudioDeviceSettings& settings, AudioDeviceSettingsDispatcher dispatcher)
    : m_settings(settings)
    , m_dispatcher(std::move(dispatcher))
    , m_settings_listener(settings, *this)
{}

// Native window close destroys the controller without a Cancel intent, so cancel here as the
// lifecycle backstop for abandoning any staged settings transaction.
AudioDeviceSettingsController::~AudioDeviceSettingsController()
{
    m_alive.reset();
    if (!m_finished)
    {
        // Destructor-only cleanup has no UI channel; the settings backend records any restore
        // failure in its state for the active settings workflow.
        [[maybe_unused]] const auto cancelled = m_settings.cancel();
    }
}

// Binds the view after construction and pushes the current settings snapshot once.
void AudioDeviceSettingsController::attachView(IAudioDeviceSettingsView& view)
{
    m_view = &view;
    updateView();
}

void AudioDeviceSettingsController::onAudioSystemSelected(int choice_id)
{
    m_settings.selectAudioSystem(choice_id);
    updateView();
}

void AudioDeviceSettingsController::onDeviceSelected(int choice_id)
{
    m_settings.selectDevice(choice_id);
    updateView();
}

void AudioDeviceSettingsController::onInputDeviceSelected(int choice_id)
{
    m_settings.selectInputDevice(choice_id);
    updateView();
}

void AudioDeviceSettingsController::onOutputDeviceSelected(int choice_id)
{
    m_settings.selectOutputDevice(choice_id);
    updateView();
}

void AudioDeviceSettingsController::onInputChannelSelected(int choice_id)
{
    m_settings.selectInputChannel(choice_id);
    updateView();
}

void AudioDeviceSettingsController::onStereoOutputPairSelected(int choice_id)
{
    m_settings.selectStereoOutputPair(choice_id);
    updateView();
}

void AudioDeviceSettingsController::onSampleRateSelected(int choice_id)
{
    m_settings.selectSampleRate(choice_id);
    updateView();
}

void AudioDeviceSettingsController::onBufferSizeSelected(int choice_id)
{
    m_settings.selectBufferSize(choice_id);
    updateView();
}

void AudioDeviceSettingsController::onControlPanelRequested()
{
    // The unavailable check mirrors the button's disabled presentation: a failed-init driver's
    // panel would silently show nothing, so the intent is refused rather than forwarded.
    if (m_last_state.control_panel_supported && !m_last_state.staged_device_error.has_value())
    {
        const auto opened = m_settings.openControlPanel();
        if (!opened.has_value())
        {
            updateView();
            return;
        }
    }
    updateView();
}

void AudioDeviceSettingsController::onOkRequested()
{
    if (!m_last_state.ok_enabled)
    {
        updateView();
        return;
    }

    if (m_dispatcher)
    {
        // Async path: disable editing now so the user sees OK take effect immediately, run apply
        // behind the dispatcher's busy indicator, and re-enable the view on failure so the
        // existing in-dialog error label can display the diagnostic. On success the host closes
        // for real via finishAndClose().
        if (m_view != nullptr)
        {
            m_view->setApplying(true);
        }
        const auto apply_succeeded = std::make_shared<bool>(false);
        m_dispatcher(
            [this, alive = std::weak_ptr<bool>{m_alive}, apply_succeeded]() {
                if (alive.expired())
                {
                    return;
                }

                const auto result = m_settings.apply();
                *apply_succeeded = result.has_value();
            },
            [this, alive = std::weak_ptr<bool>{m_alive}, apply_succeeded]() {
                if (alive.expired())
                {
                    return;
                }

                updateView();
                if (*apply_succeeded)
                {
                    finishAndClose();
                    return;
                }
                if (m_view != nullptr)
                {
                    m_view->setApplying(false);
                }
            });
        return;
    }

    const auto result = m_settings.apply();
    if (!result.has_value())
    {
        updateView();
        return;
    }

    finishAndClose();
}

void AudioDeviceSettingsController::onCommitRequested()
{
    // The live "use game audio settings" toggle already opened the desired device, so there is no
    // blocking device work to fence behind the dispatcher here: commit only clears the pending
    // restore so the active route survives window teardown, then closes.
    const auto committed = m_settings.commit();
    if (!committed.has_value())
    {
        updateView();
        return;
    }

    finishAndClose();
}

void AudioDeviceSettingsController::onCancelRequested()
{
    if (m_dispatcher)
    {
        // Async path: cancel reopens the previous audio device, which blocks the message thread
        // the same way apply does. Routing through the dispatcher gives Cancel the same dismiss-
        // immediately, busy-overlay-painted feel that OK has.
        if (m_view != nullptr)
        {
            m_view->setApplying(true);
        }
        const auto cancel_succeeded = std::make_shared<bool>(false);
        m_dispatcher(
            [this, alive = std::weak_ptr<bool>{m_alive}, cancel_succeeded]() {
                if (alive.expired())
                {
                    return;
                }

                const auto result = m_settings.cancel();
                *cancel_succeeded = result.has_value();
            },
            [this, alive = std::weak_ptr<bool>{m_alive}, cancel_succeeded]() {
                if (alive.expired())
                {
                    return;
                }

                updateView();
                if (*cancel_succeeded)
                {
                    finishAndClose();
                    return;
                }
                if (m_view != nullptr)
                {
                    m_view->setApplying(false);
                }
            });
        return;
    }

    const auto result = m_settings.cancel();
    if (!result.has_value())
    {
        updateView();
        return;
    }

    finishAndClose();
}

// External backend changes re-enter through the shared settings listener surface.
void AudioDeviceSettingsController::onAudioDeviceSettingsChanged()
{
    updateView();
}

// Pulls the shared state and sends an editor-specific state to the view.
void AudioDeviceSettingsController::updateView()
{
    m_last_state = toViewState(m_settings.state());
    if (m_view != nullptr)
    {
        m_view->setState(m_last_state);
    }
}

// Marks the edit finished before requesting close so destruction does not cancel twice.
void AudioDeviceSettingsController::finishAndClose()
{
    m_finished = true;
    if (m_view != nullptr)
    {
        m_view->requestClose();
    }
}

} // namespace rock_hero::editor::core
