#include "device/audio_device_settings.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <compare>
#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_core/juce_core.h>
#include <optional>
#include <rock_hero/common/audio/shared/scoped_listener.h>
#include <utility>

namespace rock_hero::common::audio
{

namespace
{

// JUCE reports the shared WASAPI route as the bare "Windows Audio" name; the same literal also
// serves as the family prefix used to detect any WASAPI variant.
constexpr const char* g_asio_type_name = "ASIO";
constexpr const char* g_windows_audio_type_prefix = "Windows Audio";
constexpr const char* g_windows_audio_low_latency_type_name = "Windows Audio (Low Latency Mode)";
constexpr const char* g_windows_audio_exclusive_type_name = "Windows Audio (Exclusive Mode)";
constexpr const char* g_direct_sound_type_name = "DirectSound";
constexpr const char* g_wave_out_type_name = "WaveOut";
constexpr double g_sample_rate_match_tolerance{0.001};

// Creates stable fallback text for settings failures that do not carry backend detail.
[[nodiscard]] std::string defaultErrorMessage(AudioDeviceSettingsErrorCode code)
{
    switch (code)
    {
        case AudioDeviceSettingsErrorCode::NoAudioSystem:
        {
            return "No audio system is available.";
        }
        case AudioDeviceSettingsErrorCode::NoDevice:
        {
            return "No audio device is available for the selected audio system.";
        }
        case AudioDeviceSettingsErrorCode::ApplyFailed:
        {
            return "Could not apply the selected audio device settings.";
        }
        case AudioDeviceSettingsErrorCode::RestoreFailed:
        {
            return "Could not restore the previous audio device settings.";
        }
        case AudioDeviceSettingsErrorCode::ControlPanelUnavailable:
        {
            return "The selected audio device has no control panel.";
        }
    }

    return "Could not complete the audio device settings operation.";
}

// Treats JUCE's Windows Audio modes as the WASAPI family exposed by the OS.
[[nodiscard]] bool isWasapiType(const juce::String& type_name) noexcept
{
    return type_name.startsWith(g_windows_audio_type_prefix);
}

// Ranks WASAPI variants for Rock Hero defaults. Rock Hero is a real-time guitar game where
// hardware-side latency dominates the playing experience, so Exclusive Mode wins: it bypasses
// the OS audio engine and gives the lowest WASAPI latency the hardware can offer. Low Latency
// Mode follows as the best shared-mode option (smaller buffers, event-driven). Plain Shared
// Mode sits last because its default buffer sizes give the highest WASAPI latency. Every
// variant stays visible, so users who specifically want shared mode can still select it.
[[nodiscard]] int wasapiPreferenceRank(const juce::String& type_name) noexcept
{
    if (type_name == g_windows_audio_exclusive_type_name)
    {
        return 0;
    }

    if (type_name == g_windows_audio_low_latency_type_name)
    {
        return 1;
    }

    if (type_name == g_windows_audio_type_prefix)
    {
        return 2;
    }

    return 3;
}

// Ranks backend families for Rock Hero defaults. ASIO leads because it is the standard low-latency
// Windows backend; the WASAPI band follows as the recommended modern fallback; legacy Windows
// backends sit below them. Unknown types sort last because we cannot defend ranking an
// unrecognized backend above a recognized one without knowing its real-world characteristics.
[[nodiscard]] int deviceTypePreferenceRank(const juce::String& type_name) noexcept
{
    if (type_name == g_asio_type_name)
    {
        return 0;
    }

    if (isWasapiType(type_name))
    {
        return 10 + wasapiPreferenceRank(type_name);
    }

    if (type_name == g_direct_sound_type_name)
    {
        return 30;
    }

    if (type_name == g_wave_out_type_name)
    {
        return 40;
    }

    return 50;
}

// Collects JUCE device type names so shared settings policy can order by stable names.
[[nodiscard]] juce::StringArray availableDeviceTypeNames(
    const juce::OwnedArray<juce::AudioIODeviceType>& device_types)
{
    juce::StringArray type_names;
    for (const auto* device_type : device_types)
    {
        type_names.add(device_type->getTypeName());
    }

    return type_names;
}

// Applies Rock Hero's default settings order to JUCE's discovered device-system names.
[[nodiscard]] juce::StringArray preferredAudioDeviceTypeOrder(
    const juce::StringArray& available_type_names)
{
    // Copy into std::vector because juce::StringArray::sort accepts no custom comparator; the
    // round trip is the smallest way to apply a rank-based ordering.
    std::vector<juce::String> ordered_type_names;
    ordered_type_names.reserve(static_cast<std::size_t>(available_type_names.size()));

    for (const auto& type_name : available_type_names)
    {
        ordered_type_names.push_back(type_name);
    }

    std::ranges::stable_sort(ordered_type_names, [](const auto& lhs, const auto& rhs) {
        return deviceTypePreferenceRank(lhs) < deviceTypePreferenceRank(rhs);
    });

    juce::StringArray result;
    for (const auto& type_name : ordered_type_names)
    {
        result.add(type_name);
    }
    return result;
}

// Keeps hardware-rate comparisons consistent across policy and selected-choice derivation.
[[nodiscard]] bool sampleRatesMatch(double lhs, double rhs) noexcept
{
    return std::abs(lhs - rhs) < g_sample_rate_match_tolerance;
}

// True when the rate list contains a value close enough to be considered the same selection.
[[nodiscard]] bool containsSampleRate(const std::vector<double>& rates, double rate)
{
    return std::ranges::any_of(
        rates, [rate](double available) { return sampleRatesMatch(available, rate); });
}

// Picks 48 kHz, then 44.1 kHz, then the first available rate as the studio-standard fallback.
[[nodiscard]] double fallbackSampleRate(const std::vector<double>& rates)
{
    constexpr std::array preferred_rates{48000.0, 44100.0};
    for (const double rate : preferred_rates)
    {
        if (containsSampleRate(rates, rate))
        {
            return rate;
        }
    }
    return rates.empty() ? 0.0 : rates.front();
}

// Chooses a rate from route-specific hints before falling back to studio-standard defaults.
[[nodiscard]] double chooseAudioDeviceSampleRate(
    const std::vector<double>& available_rates, double staged_rate, double preview_device_rate,
    std::optional<double> active_route_rate)
{
    for (const double candidate : {staged_rate, preview_device_rate})
    {
        if (candidate > 0.0 && containsSampleRate(available_rates, candidate))
        {
            return candidate;
        }
    }

    if (active_route_rate.has_value() && *active_route_rate > 0.0 &&
        containsSampleRate(available_rates, *active_route_rate))
    {
        return *active_route_rate;
    }

    return fallbackSampleRate(available_rates);
}

// Converts JUCE strings into standard strings for the project-owned settings state.
[[nodiscard]] std::vector<std::string> toStrings(const juce::StringArray& values)
{
    std::vector<std::string> result;
    result.reserve(static_cast<std::size_t>(values.size()));
    for (const auto& value : values)
    {
        result.push_back(value.toStdString());
    }

    return result;
}

// Returns a readable channel name, falling back to a numbered label when the driver omits one.
[[nodiscard]] juce::String channelName(
    const juce::StringArray& channel_names, int channel_index, const juce::String& fallback_prefix)
{
    if (juce::isPositiveAndBelow(channel_index, channel_names.size()) &&
        channel_names[channel_index].isNotEmpty())
    {
        return channel_names[channel_index];
    }

    return fallback_prefix + " " + juce::String{channel_index + 1};
}

// Formats a stereo output pair as a single app-level route.
[[nodiscard]] juce::String outputPairName(
    const juce::StringArray& channel_names, int left_channel, int right_channel)
{
    return channelName(channel_names, left_channel, "Output") + " + " +
           channelName(channel_names, right_channel, "Output");
}

// Keeps a requested device name when it still exists, otherwise picks the driver's default.
[[nodiscard]] juce::String validOrDefaultDeviceName(
    const juce::String& requested_name, const juce::StringArray& device_names, int default_index)
{
    if (device_names.contains(requested_name))
    {
        return requested_name;
    }

    if (juce::isPositiveAndBelow(default_index, device_names.size()))
    {
        return device_names[default_index];
    }

    return device_names.isEmpty() ? juce::String{} : device_names[0];
}

// Returns the first one-based choice ID whose text matches the selected value.
[[nodiscard]] int selectedStringId(
    const juce::StringArray& choices, const juce::String& selected_text) noexcept
{
    const int selected_index = choices.indexOf(selected_text);
    return selected_index >= 0 ? selected_index + 1 : 0;
}

// Finds the first sample-rate choice that matches the staged rate closely enough.
[[nodiscard]] int selectedSampleRateId(
    const std::vector<double>& sample_rates, double current_rate) noexcept
{
    for (int index = 0; std::cmp_less(index, sample_rates.size()); ++index)
    {
        if (sampleRatesMatch(sample_rates[static_cast<std::size_t>(index)], current_rate))
        {
            return index + 1;
        }
    }

    return 0;
}

// Finds the first buffer-size choice that matches the staged buffer size.
[[nodiscard]] int selectedBufferSizeId(
    const std::vector<int>& buffer_sizes, int current_size) noexcept
{
    for (int index = 0; std::cmp_less(index, buffer_sizes.size()); ++index)
    {
        if (buffer_sizes[static_cast<std::size_t>(index)] == current_size)
        {
            return index + 1;
        }
    }

    return 0;
}

} // namespace

AudioDeviceSettingsError::AudioDeviceSettingsError(AudioDeviceSettingsErrorCode error_code)
    : code(error_code)
    , message(defaultErrorMessage(error_code))
{}

AudioDeviceSettingsError::AudioDeviceSettingsError(
    AudioDeviceSettingsErrorCode error_code, std::string message_text)
    : code(error_code)
    , message(std::move(message_text))
{}

struct AudioDeviceSettings::Impl final : IAudioDeviceConfiguration::Listener
{
    explicit Impl(IAudioDeviceConfiguration& audio_devices)
        : m_device_manager(audio_devices.deviceManager())
        , m_configuration_listener(audio_devices, *this)
    {
        captureInitialRouteAndCloseDevice();
    }

    // Reopens the previous route only if construction saw an actually-open device. The controller
    // calls cancel() on a non-finished native close, but a destructor-only path can still happen
    // (for example tests that tear down without driving a cancel). Reopening unconditionally when
    // the device is closed would accidentally start audio in cases where it was already closed
    // before the settings edit began.
    ~Impl() noexcept override
    {
        try
        {
            restorePreviousRouteBestEffort();
        }
        catch (...)
        {
            m_restore_pending = false;
        }
    }

    Impl(const Impl&) = delete;
    Impl& operator=(const Impl&) = delete;
    Impl(Impl&&) = delete;
    Impl& operator=(Impl&&) = delete;

    // Captures the active route as the "previous" snapshot, then closes the audio device so the
    // user can edit hardware settings without holding it. Subsequent apply() opens the staged
    // route; cancel() reopens this captured snapshot when the route was actually open.
    //
    // m_restore_pending records whether there is something meaningful to restore. JUCE preserves
    // currentSetup and currentDeviceType across closeAudioDevice(), so without the explicit flag
    // we cannot tell "device was open before editing" from "device was already closed and
    // we should leave it that way."
    void captureInitialRouteAndCloseDevice()
    {
        m_previous_setup = m_device_manager.getAudioDeviceSetup();
        m_previous_device_type = m_device_manager.getCurrentAudioDeviceType();
        m_restore_pending = m_device_manager.getCurrentAudioDevice() != nullptr;

        m_staged_setup = m_previous_setup;
        m_staged_device_type = m_previous_device_type;

        if (m_restore_pending)
        {
            m_device_manager.closeAudioDevice();
        }

        refreshState({});
    }

    // Returns the last derived project-owned settings snapshot.
    [[nodiscard]] AudioDeviceSettingsState state() const
    {
        return m_state;
    }

    // Selects a backend family and resets dependent route and format fields.
    void selectAudioSystem(int choice_id)
    {
        const juce::StringArray type_names = orderedDeviceTypeNames();
        if (!juce::isPositiveAndBelow(choice_id - 1, type_names.size()))
        {
            return;
        }

        const juce::String& type_name = type_names[choice_id - 1];
        if (type_name == m_staged_device_type)
        {
            return;
        }

        m_staged_device_type = type_name;
        m_staged_setup.inputDeviceName.clear();
        m_staged_setup.outputDeviceName.clear();
        m_staged_setup.sampleRate = 0.0;
        m_staged_setup.bufferSize = 0;
        resetStagedRouteDefaults();
        refreshState({});
    }

    // Selects one combined device for backends such as ASIO.
    void selectDevice(int choice_id)
    {
        if (!juce::isPositiveAndBelow(choice_id - 1, static_cast<int>(m_state.devices.size())))
        {
            return;
        }

        const juce::String device_name{m_state.devices[static_cast<std::size_t>(choice_id - 1)]};
        m_staged_setup.inputDeviceName = device_name;
        m_staged_setup.outputDeviceName = device_name;
        resetFormatAndRouteDefaults();
        refreshState({});
    }

    // Selects the input side of a split input/output backend.
    void selectInputDevice(int choice_id)
    {
        if (!juce::isPositiveAndBelow(
                choice_id - 1, static_cast<int>(m_state.input_devices.size())))
        {
            return;
        }

        m_staged_setup.inputDeviceName =
            juce::String{m_state.input_devices[static_cast<std::size_t>(choice_id - 1)]};
        resetFormatAndRouteDefaults();
        refreshState({});
    }

    // Selects the output side of a split input/output backend.
    void selectOutputDevice(int choice_id)
    {
        if (!juce::isPositiveAndBelow(
                choice_id - 1, static_cast<int>(m_state.output_devices.size())))
        {
            return;
        }

        m_staged_setup.outputDeviceName =
            juce::String{m_state.output_devices[static_cast<std::size_t>(choice_id - 1)]};
        resetFormatAndRouteDefaults();
        refreshState({});
    }

    // Replaces the staged input mask with exactly one mono channel.
    void selectInputChannel(int choice_id)
    {
        if (!juce::isPositiveAndBelow(
                choice_id - 1, static_cast<int>(m_state.input_channels.size())))
        {
            return;
        }

        setSingleInputChannel(choice_id - 1);
        refreshState({});
    }

    // Replaces the staged output mask with the chosen stereo pair.
    void selectStereoOutputPair(int choice_id)
    {
        if (!juce::isPositiveAndBelow(
                choice_id - 1, static_cast<int>(m_state.stereo_output_pairs.size())))
        {
            return;
        }

        const StereoOutputPair& pair =
            m_state.stereo_output_pairs[static_cast<std::size_t>(choice_id - 1)];
        setOutputPair(pair.left_channel, pair.right_channel);
        refreshState({});
    }

    // Stores the selected sample rate directly on the staged setup.
    void selectSampleRate(int choice_id)
    {
        if (!juce::isPositiveAndBelow(choice_id - 1, static_cast<int>(m_state.sample_rates.size())))
        {
            return;
        }

        m_staged_setup.sampleRate = m_state.sample_rates[static_cast<std::size_t>(choice_id - 1)];
        refreshState({});
    }

    // Stores the selected buffer size directly on the staged setup.
    void selectBufferSize(int choice_id)
    {
        if (!juce::isPositiveAndBelow(choice_id - 1, static_cast<int>(m_state.buffer_sizes.size())))
        {
            return;
        }

        m_staged_setup.bufferSize = m_state.buffer_sizes[static_cast<std::size_t>(choice_id - 1)];
        refreshState({});
    }

    // Opens the staged route. If the open fails, leaves the backend closed. With the device
    // closed during the settings edit, setCurrentAudioDeviceType() does not incur JUCE's 1.5
    // second open-device release sleep.
    [[nodiscard]] std::expected<void, AudioDeviceSettingsError> apply()
    {
        if (m_staged_device_type.isEmpty())
        {
            AudioDeviceSettingsError error{AudioDeviceSettingsErrorCode::NoAudioSystem};
            refreshState(error.message);
            return std::unexpected{std::move(error)};
        }

        if (m_staged_setup.inputDeviceName.isEmpty() || m_staged_setup.outputDeviceName.isEmpty())
        {
            AudioDeviceSettingsError error{AudioDeviceSettingsErrorCode::NoDevice};
            refreshState(error.message);
            return std::unexpected{std::move(error)};
        }

        if (m_staged_device_type != m_device_manager.getCurrentAudioDeviceType())
        {
            m_device_manager.setCurrentAudioDeviceType(m_staged_device_type, true);
        }

        const juce::String error_text = m_device_manager.setAudioDeviceSetup(m_staged_setup, true);
        if (error_text.isEmpty())
        {
            // Staged route is now the active route. The captured previous route is no longer
            // meaningful, so destruction should not try to restore it.
            m_restore_pending = false;
            refreshState({});
            return {};
        }

        m_device_manager.closeAudioDevice();
        m_restore_pending = false;
        AudioDeviceSettingsError error{
            AudioDeviceSettingsErrorCode::ApplyFailed, error_text.toStdString()
        };
        refreshState(error.message);
        return std::unexpected{std::move(error)};
    }

    // Reopens the captured audio device when there was one to reopen. No-op when settings were
    // opened from an [audio device closed] state, so cancel cannot accidentally start audio that
    // was not running before the settings edit.
    [[nodiscard]] std::expected<void, AudioDeviceSettingsError> cancel()
    {
        auto restored = restorePreviousRoute();
        if (!restored.has_value())
        {
            refreshState(restored.error().message);
            return std::unexpected{std::move(restored.error())};
        }

        m_restore_pending = false;
        refreshState({});
        return {};
    }

    // Keeps the live route as final: clears the pending restore so ~Impl() does not reopen the
    // captured previous route, then rebuilds derived state. No device work runs because the active
    // route is already the one the user is keeping (it was opened out of band while the window was
    // open, for example by the editor's live "use game audio settings" toggle).
    [[nodiscard]] std::expected<void, AudioDeviceSettingsError> commit()
    {
        m_restore_pending = false;
        refreshState({});
        return {};
    }

    // Opens the staged backend's control panel through the in-memory staged device so the panel
    // remains available even though the active audio device is closed during the settings
    // edit. ASIO drivers honor showControlPanel() against a non-open device because the type
    // has already loaded the driver to populate the staged device's capability set.
    [[nodiscard]] std::expected<void, AudioDeviceSettingsError> openControlPanel()
    {
        // staged_device_unavailable joins the guard because a failed-init driver's
        // showControlPanel() is a silent no-op; erroring here keeps a raced click (state refresh
        // pending) from doing visibly nothing.
        if (!m_state.control_panel_supported || m_state.staged_device_unavailable ||
            m_staged_device == nullptr)
        {
            AudioDeviceSettingsError error{AudioDeviceSettingsErrorCode::ControlPanelUnavailable};
            refreshState(error.message);
            return std::unexpected{std::move(error)};
        }

        // showControlPanel()'s bool is not a success/failure signal. For ASIO it opens the panel
        // unconditionally (via the driver's controlPanel() call) and returns true only when the
        // user dwelt in the panel long enough (>300ms) to justify reloading preferred buffer
        // sizes; a quick open/close, or a non-blocking driver, returns false even though the panel
        // opened. hasControlPanel() (already checked via control_panel_supported above) is the
        // only reliable capability signal, so the return value is discarded rather than treated as
        // a failure.
        m_staged_device->showControlPanel();

        refreshState({});
        return {};
    }

    // Registers a settings listener for external backend refreshes.
    void addListener(IAudioDeviceSettings::Listener& listener)
    {
        m_listeners.add(&listener);
    }

    // Removes a previously registered settings listener.
    void removeListener(IAudioDeviceSettings::Listener& listener)
    {
        m_listeners.remove(&listener);
    }

    // Keeps staged settings valid when the underlying backend changes outside this workflow.
    // Preserves the current error_message across the refresh: apply() sets the error on m_state
    // before returning, then JUCE delivers its async route-change broadcast a moment later. An
    // unconditional empty error here would clobber that just-set diagnostic, making OK failures
    // appear to silently succeed. User-action paths (select*, apply, cancel) refresh with an
    // explicit empty error so transient diagnostics still clear on the next interaction.
    void onAudioDeviceConfigurationChanged() override
    {
        invalidateDeviceScanCache();
        refreshState(m_state.error_message);
        m_listeners.call(&IAudioDeviceSettings::Listener::onAudioDeviceSettingsChanged);
    }

private:
    // Reopens the captured route and translates JUCE's setup error string when restore fails.
    [[nodiscard]] std::expected<void, AudioDeviceSettingsError> restorePreviousRoute()
    {
        if (!m_restore_pending || m_previous_device_type.isEmpty())
        {
            return {};
        }

        if (m_previous_device_type != m_device_manager.getCurrentAudioDeviceType())
        {
            m_device_manager.setCurrentAudioDeviceType(m_previous_device_type, true);
        }

        const juce::String error_text =
            m_device_manager.setAudioDeviceSetup(m_previous_setup, true);
        if (error_text.isEmpty())
        {
            return {};
        }

        return std::unexpected{AudioDeviceSettingsError{
            AudioDeviceSettingsErrorCode::RestoreFailed, error_text.toStdString()
        }};
    }

    // Destructor cleanup has no caller-visible channel, so restore failure is intentionally
    // best-effort after recording the diagnostic in state for any surviving listener snapshot.
    void restorePreviousRouteBestEffort()
    {
        if (!m_restore_pending)
        {
            return;
        }

        const auto restored = restorePreviousRoute();
        if (!restored.has_value())
        {
            refreshState(restored.error().message);
        }

        m_restore_pending = false;
    }

    // Rebuilds the public settings state while preserving a caller-supplied operation error.
    void refreshState(std::string error_message)
    {
        ensureStagedDeviceType();
        scanCurrentDeviceTypeIfNeeded();
        ensureStagedDeviceNames();
        refreshStagedDeviceIfRouteChanged();

        AudioDeviceSettingsState next_state;
        next_state.error_message = std::move(error_message);

        refreshDeviceTypes(next_state);
        refreshDeviceNames(next_state);
        refreshChannelChoices(next_state);
        refreshSampleRateChoices(next_state);
        refreshBufferSizeChoices(next_state);
        refreshCapabilities(next_state);

        m_state = std::move(next_state);
    }

    // Returns audio systems ordered according to Rock Hero's default preference.
    [[nodiscard]] juce::StringArray orderedDeviceTypeNames() const
    {
        return preferredAudioDeviceTypeOrder(
            availableDeviceTypeNames(m_device_manager.getAvailableDeviceTypes()));
    }

    // Chooses a staged audio system from the active device manager's available types.
    void ensureStagedDeviceType()
    {
        const juce::StringArray type_names = orderedDeviceTypeNames();
        if (type_names.contains(m_staged_device_type))
        {
            return;
        }

        const juce::String active_device_type = m_device_manager.getCurrentAudioDeviceType();
        if (type_names.contains(active_device_type))
        {
            m_staged_device_type = active_device_type;
            return;
        }

        m_staged_device_type = type_names.isEmpty() ? juce::String{} : type_names[0];
    }

    // Scans the current device type's device list only when the staged audio system has changed
    // since the last scan or an external configuration broadcast has invalidated the cache.
    // JUCE's scanForDevices is slow on WASAPI (hundreds of ms enumerating MMDevice endpoints), so
    // calling it on every refreshState made apply() visibly laggy: repeated user-action refreshes
    // would each rescan even though the audio system is unchanged.
    void scanCurrentDeviceTypeIfNeeded()
    {
        if (m_last_scanned_device_type == m_staged_device_type)
        {
            return;
        }

        if (juce::AudioIODeviceType* const type = currentDeviceType(); type != nullptr)
        {
            type->scanForDevices();
            m_last_scanned_device_type = m_staged_device_type;
        }
    }

    // Forces the next refresh to rescan device names and rebuild capability probes after a
    // backend broadcast. The staged audio system may not change during same-backend hot-plug
    // events, but endpoint identity can shift, so the single-entry staged-device cache is
    // dropped as well.
    void invalidateDeviceScanCache()
    {
        m_last_scanned_device_type.clear();
        m_staged_device.reset();
        m_cached_staged_device_type.clear();
        m_cached_staged_input_device_name.clear();
        m_cached_staged_output_device_name.clear();
    }

    // Re-creates the staged preview device only when the audio system or device names change.
    // Format-only changes (sample rate, buffer size) and listener notifications that re-broadcast
    // the same route therefore skip the rebuild. This is not about speed; it is about not doing
    // unnecessary work when only the format part of the setup changed. Device-name changes
    // unavoidably trigger a fresh JUCE createDevice and capability probe.
    void refreshStagedDeviceIfRouteChanged()
    {
        if (m_staged_device != nullptr && m_cached_staged_device_type == m_staged_device_type &&
            m_cached_staged_input_device_name == m_staged_setup.inputDeviceName &&
            m_cached_staged_output_device_name == m_staged_setup.outputDeviceName)
        {
            return;
        }

        m_staged_device = createStagedDevice();
        m_cached_staged_device_type = m_staged_device_type;
        m_cached_staged_input_device_name = m_staged_setup.inputDeviceName;
        m_cached_staged_output_device_name = m_staged_setup.outputDeviceName;
    }

    // Keeps staged device names valid for the selected audio system without opening the active
    // route.
    void ensureStagedDeviceNames()
    {
        auto* type = currentDeviceType();
        if (type == nullptr)
        {
            m_staged_setup.inputDeviceName.clear();
            m_staged_setup.outputDeviceName.clear();
            return;
        }

        if (type->hasSeparateInputsAndOutputs())
        {
            m_staged_setup.inputDeviceName = validOrDefaultDeviceName(
                m_staged_setup.inputDeviceName,
                type->getDeviceNames(true),
                type->getDefaultDeviceIndex(true));
            m_staged_setup.outputDeviceName = validOrDefaultDeviceName(
                m_staged_setup.outputDeviceName,
                type->getDeviceNames(false),
                type->getDefaultDeviceIndex(false));
            return;
        }

        auto device_names = type->getDeviceNames(false);
        if (device_names.isEmpty())
        {
            device_names = type->getDeviceNames(true);
        }

        const juce::String requested_name = m_staged_setup.outputDeviceName.isNotEmpty()
                                                ? m_staged_setup.outputDeviceName
                                                : m_staged_setup.inputDeviceName;
        const juce::String device_name = validOrDefaultDeviceName(
            requested_name, device_names, type->getDefaultDeviceIndex(false));
        m_staged_setup.inputDeviceName = device_name;
        m_staged_setup.outputDeviceName = device_name;
    }

    // Populates audio-system choices and selected ID.
    void refreshDeviceTypes(AudioDeviceSettingsState& next_state) const
    {
        const juce::StringArray type_names = orderedDeviceTypeNames();
        next_state.audio_systems = toStrings(type_names);
        next_state.selected_audio_system_id = selectedStringId(type_names, m_staged_device_type);
    }

    // Populates either combined device choices or split input/output device choices.
    void refreshDeviceNames(AudioDeviceSettingsState& next_state) const
    {
        auto* type = currentDeviceType();
        next_state.uses_separate_input_output_devices =
            type != nullptr && type->hasSeparateInputsAndOutputs();

        if (type == nullptr)
        {
            return;
        }

        if (next_state.uses_separate_input_output_devices)
        {
            const juce::StringArray input_names = type->getDeviceNames(true);
            const juce::StringArray output_names = type->getDeviceNames(false);
            next_state.input_devices = toStrings(input_names);
            next_state.output_devices = toStrings(output_names);
            next_state.selected_input_device_id =
                selectedStringId(input_names, m_staged_setup.inputDeviceName);
            next_state.selected_output_device_id =
                selectedStringId(output_names, m_staged_setup.outputDeviceName);
            return;
        }

        auto device_names = type->getDeviceNames(false);
        if (device_names.isEmpty())
        {
            device_names = type->getDeviceNames(true);
        }

        const juce::String selected_device = m_staged_setup.outputDeviceName.isNotEmpty()
                                                 ? m_staged_setup.outputDeviceName
                                                 : m_staged_setup.inputDeviceName;
        next_state.devices = toStrings(device_names);
        next_state.selected_device_id = selectedStringId(device_names, selected_device);
    }

    // Populates mono input choices and app-level stereo output-pair choices.
    void refreshChannelChoices(AudioDeviceSettingsState& next_state) const
    {
        auto* device = m_staged_device.get();
        if (device == nullptr)
        {
            return;
        }

        const auto input_names = device->getInputChannelNames();
        next_state.input_channels.reserve(static_cast<std::size_t>(input_names.size()));
        for (int index = 0; index < input_names.size(); ++index)
        {
            next_state.input_channels.push_back(
                channelName(input_names, index, "Input").toStdString());
        }

        const int selected_input = m_staged_setup.inputChannels.findNextSetBit(0);
        next_state.selected_input_channel_id =
            juce::isPositiveAndBelow(selected_input, input_names.size()) ? selected_input + 1 : 0;

        const auto output_names = device->getOutputChannelNames();
        for (int channel_index = 0; channel_index + 1 < output_names.size(); channel_index += 2)
        {
            next_state.stereo_output_pairs.push_back(
                StereoOutputPair{
                    .left_channel = channel_index,
                    .right_channel = channel_index + 1,
                    .label = outputPairName(output_names, channel_index, channel_index + 1)
                                 .toStdString(),
                });
        }

        int selected_output_pair_id = next_state.stereo_output_pairs.empty() ? 0 : 1;
        for (int index = 0; std::cmp_less(index, next_state.stereo_output_pairs.size()); ++index)
        {
            const auto& pair = next_state.stereo_output_pairs[static_cast<std::size_t>(index)];
            if (m_staged_setup.outputChannels[pair.left_channel] &&
                m_staged_setup.outputChannels[pair.right_channel])
            {
                selected_output_pair_id = index + 1;
                break;
            }
        }

        next_state.selected_stereo_output_pair_id = selected_output_pair_id;
    }

    // Populates sample-rate choices from the staged device capabilities.
    void refreshSampleRateChoices(AudioDeviceSettingsState& next_state)
    {
        auto* device = m_staged_device.get();
        if (device == nullptr)
        {
            return;
        }

        for (const auto sample_rate : device->getAvailableSampleRates())
        {
            next_state.sample_rates.push_back(sample_rate);
        }

        // The staged preview device may not be open and may therefore not report a current rate.
        // If the staged route matches the currently open route, the active device's reported rate
        // is the same physical device's current rate and is safe to borrow as a fallback.
        std::optional<double> active_route_rate;
        if (stagedDeviceNamesMatchActiveRoute())
        {
            if (auto* active_device = m_device_manager.getCurrentAudioDevice();
                active_device != nullptr)
            {
                active_route_rate = active_device->getCurrentSampleRate();
            }
        }

        const double selected_sample_rate = chooseAudioDeviceSampleRate(
            next_state.sample_rates,
            m_staged_setup.sampleRate,
            device->getCurrentSampleRate(),
            active_route_rate);

        if (m_staged_setup.sampleRate <= 0.0 && selected_sample_rate > 0.0)
        {
            m_staged_setup.sampleRate = selected_sample_rate;
        }

        next_state.selected_sample_rate_id =
            selectedSampleRateId(next_state.sample_rates, selected_sample_rate);
    }

    // Populates buffer-size choices from the staged device capabilities.
    void refreshBufferSizeChoices(AudioDeviceSettingsState& next_state)
    {
        auto* device = m_staged_device.get();
        if (device == nullptr)
        {
            return;
        }

        if (m_staged_setup.bufferSize <= 0)
        {
            const int current_buffer_size = device->getCurrentBufferSizeSamples();
            m_staged_setup.bufferSize =
                current_buffer_size > 0 ? current_buffer_size : device->getDefaultBufferSize();
        }

        for (const auto buffer_size : device->getAvailableBufferSizes())
        {
            next_state.buffer_sizes.push_back(buffer_size);
        }

        next_state.selected_buffer_size_id = selectedBufferSizeId(
            next_state.buffer_sizes,
            m_staged_setup.bufferSize > 0 ? m_staged_setup.bufferSize
                                          : device->getCurrentBufferSizeSamples());
    }

    // Derives capability flags from the staged preview device. The settings edit keeps the
    // active device closed, so capability gating is based on what the staged device (the loaded
    // driver object behind the user's current selection) reports, not on the now-closed active
    // device.
    //
    // hasControlPanel() is a driver-class capability, not actionability: an ASIO driver whose
    // construction-time init failed (hardware unplugged, or the device held by another
    // application) still returns true, but its showControlPanel() silently no-ops because JUCE
    // guards the driver call on the initialized driver object (verified in juce_ASIO_windows.cpp:
    // hasControlPanel() is unconditionally true, showControlPanel() checks asioObject, and a
    // failed constructor-time openDevice() leaves getLastError() non-empty). The staged preview
    // device is never stream-opened, so a non-empty last error means exactly that failed init;
    // it is surfaced as staged_device_unavailable so the control panel can gray out honestly.
    void refreshCapabilities(AudioDeviceSettingsState& next_state) const
    {
        next_state.control_panel_supported =
            m_staged_device != nullptr && m_staged_device->hasControlPanel();
        next_state.staged_device_unavailable =
            m_staged_device != nullptr && m_staged_device->getLastError().isNotEmpty();
    }

    // Clears route and format fields whose choices depend on the selected device.
    void resetFormatAndRouteDefaults()
    {
        m_staged_setup.sampleRate = 0.0;
        m_staged_setup.bufferSize = 0;
        resetStagedRouteDefaults();
    }

    // Sets the staged route to one mono input and one stereo output pair.
    void resetStagedRouteDefaults()
    {
        setSingleInputChannel(0);
        setOutputPair(0, 1);
    }

    // Returns the currently selected JUCE device type object, if one exists.
    [[nodiscard]] juce::AudioIODeviceType* currentDeviceType() const
    {
        const auto& device_types = m_device_manager.getAvailableDeviceTypes();
        for (auto* type : device_types)
        {
            if (type->getTypeName() == m_staged_device_type)
            {
                return type;
            }
        }

        return nullptr;
    }

    // Creates a non-open staged device used only to inspect channel and format capabilities.
    [[nodiscard]] std::unique_ptr<juce::AudioIODevice> createStagedDevice() const
    {
        auto* type = currentDeviceType();
        if (type == nullptr || m_staged_setup.inputDeviceName.isEmpty() ||
            m_staged_setup.outputDeviceName.isEmpty())
        {
            return nullptr;
        }

        return std::unique_ptr<juce::AudioIODevice>{type->createDevice(
            m_staged_setup.outputDeviceName, m_staged_setup.inputDeviceName)};
    }

    // Matches the currently selected device names while ignoring route and format staging edits.
    // The active device is closed during a settings edit, so this returns false during the edit
    // itself. It still returns true on the trailing refreshState() that runs after a
    // successful apply, when the active route again matches the staged names.
    [[nodiscard]] bool stagedDeviceNamesMatchActiveRoute() const
    {
        const auto active_setup = m_device_manager.getAudioDeviceSetup();
        return m_staged_device_type == m_device_manager.getCurrentAudioDeviceType() &&
               m_staged_setup.inputDeviceName == active_setup.inputDeviceName &&
               m_staged_setup.outputDeviceName == active_setup.outputDeviceName;
    }

    // Replaces the staged input channels with exactly one mono input channel.
    void setSingleInputChannel(int channel_index)
    {
        m_staged_setup.useDefaultInputChannels = false;
        m_staged_setup.inputChannels.clear();
        if (channel_index >= 0)
        {
            m_staged_setup.inputChannels.setBit(channel_index);
        }
    }

    // Replaces the staged output channels with exactly one stereo output pair.
    void setOutputPair(int left_channel, int right_channel)
    {
        m_staged_setup.useDefaultOutputChannels = false;
        m_staged_setup.outputChannels.clear();
        m_staged_setup.outputChannels.setBit(left_channel);
        m_staged_setup.outputChannels.setBit(right_channel);
    }

    // Audio device manager owned by the shared backend.
    juce::AudioDeviceManager& m_device_manager;

    // Route active when the settings edit was constructed. cancel() and destructor fallback
    // reopen this until an apply open attempt succeeds or fails.
    juce::AudioDeviceManager::AudioDeviceSetup m_previous_setup;
    juce::String m_previous_device_type;

    // True between construction and an apply open attempt or cancel when there was an
    // actually-open device to restore. Gates cancel() and ~Impl() so they cannot accidentally
    // start audio from an originally-closed state.
    bool m_restore_pending{false};

    // Staged route edited independently from the active device manager until apply().
    juce::AudioDeviceManager::AudioDeviceSetup m_staged_setup;
    juce::String m_staged_device_type;
    std::unique_ptr<juce::AudioIODevice> m_staged_device;

    // Audio system name passed to the most recent scanForDevices call; used by
    // scanCurrentDeviceTypeIfNeeded to avoid rescanning during apply-triggered refreshes.
    juce::String m_last_scanned_device_type;

    // Audio system and device names that produced the current m_staged_device; used by
    // refreshStagedDeviceIfRouteChanged to skip rebuilding the preview device when only the
    // format (sample rate, buffer size) changed. Device-name changes still trigger a full
    // createDevice and capability probe.
    juce::String m_cached_staged_device_type;
    juce::String m_cached_staged_input_device_name;
    juce::String m_cached_staged_output_device_name;

    // Last project-owned state snapshot returned by state().
    AudioDeviceSettingsState m_state{};

    // Listener surface for product controllers that need to refresh after external changes.
    juce::ListenerList<IAudioDeviceSettings::Listener> m_listeners;

    // Declared last so listener deregistration runs before referenced state is destroyed.
    ScopedListener<IAudioDeviceConfiguration, IAudioDeviceConfiguration::Listener>
        m_configuration_listener;
};

AudioDeviceSettings::AudioDeviceSettings(IAudioDeviceConfiguration& audio_devices)
    : m_impl(std::make_unique<Impl>(audio_devices))
{}

AudioDeviceSettings::~AudioDeviceSettings() = default;

AudioDeviceSettingsState AudioDeviceSettings::state() const
{
    return m_impl->state();
}

void AudioDeviceSettings::selectAudioSystem(int choice_id)
{
    m_impl->selectAudioSystem(choice_id);
}

void AudioDeviceSettings::selectDevice(int choice_id)
{
    m_impl->selectDevice(choice_id);
}

void AudioDeviceSettings::selectInputDevice(int choice_id)
{
    m_impl->selectInputDevice(choice_id);
}

void AudioDeviceSettings::selectOutputDevice(int choice_id)
{
    m_impl->selectOutputDevice(choice_id);
}

void AudioDeviceSettings::selectInputChannel(int choice_id)
{
    m_impl->selectInputChannel(choice_id);
}

void AudioDeviceSettings::selectStereoOutputPair(int choice_id)
{
    m_impl->selectStereoOutputPair(choice_id);
}

void AudioDeviceSettings::selectSampleRate(int choice_id)
{
    m_impl->selectSampleRate(choice_id);
}

void AudioDeviceSettings::selectBufferSize(int choice_id)
{
    m_impl->selectBufferSize(choice_id);
}

std::expected<void, AudioDeviceSettingsError> AudioDeviceSettings::apply()
{
    return m_impl->apply();
}

std::expected<void, AudioDeviceSettingsError> AudioDeviceSettings::cancel()
{
    return m_impl->cancel();
}

std::expected<void, AudioDeviceSettingsError> AudioDeviceSettings::commit()
{
    return m_impl->commit();
}

std::expected<void, AudioDeviceSettingsError> AudioDeviceSettings::openControlPanel()
{
    return m_impl->openControlPanel();
}

void AudioDeviceSettings::addListener(Listener& listener)
{
    m_impl->addListener(listener);
}

void AudioDeviceSettings::removeListener(Listener& listener)
{
    m_impl->removeListener(listener);
}

} // namespace rock_hero::common::audio
