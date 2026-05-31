/*!
\file configurable_audio_device_configuration.h
\brief Configurable audio-device configuration test implementation.
*/

#pragma once

#include <juce_audio_devices/juce_audio_devices.h>
#include <optional>
#include <rock_hero/common/audio/i_audio_device_configuration.h>
#include <string>
#include <vector>

namespace rock_hero::common::audio::testing
{

/*!
\brief IAudioDeviceConfiguration implementation backed by a real JUCE device manager.

Use this when tests need the production audio-device settings surface without constructing the
full audio engine. Tests can configure the current project-owned status, configure the current
input identity, and trigger listener notifications explicitly.
*/
class ConfigurableAudioDeviceConfiguration final : public IAudioDeviceConfiguration
{
public:
    /*! \brief Returns the fake-owned device manager for settings UI and policy tests. */
    [[nodiscard]] juce::AudioDeviceManager& deviceManager() noexcept override
    {
        return device_manager;
    }

    /*! \brief Records a serialized state restore request and returns the configured result. */
    [[nodiscard]] std::expected<void, AudioDeviceConfigurationError> restoreSerializedDeviceState(
        const std::string& serialized_state) override
    {
        last_restored_serialized_device_state = serialized_state;
        restore_serialized_device_state_call_count += 1;
        if (next_restore_serialized_device_state_error.has_value())
        {
            AudioDeviceConfigurationError error =
                std::move(*next_restore_serialized_device_state_error);
            next_restore_serialized_device_state_error.reset();
            return std::unexpected{std::move(error)};
        }

        if (!restore_serialized_device_state_result)
        {
            return std::unexpected{AudioDeviceConfigurationError{
                AudioDeviceConfigurationErrorCode::RestoreFailed,
                "Configured serialized audio-device restore failure",
            }};
        }

        return {};
    }

    /*! \brief Returns the configured serialized state capture result. */
    [[nodiscard]] std::optional<std::string> serializedDeviceState() const override
    {
        serialized_device_state_call_count += 1;
        return serialized_device_state;
    }

    /*! \brief Returns the configured current device status snapshot. */
    [[nodiscard]] AudioDeviceStatus currentDeviceStatus() const override
    {
        status_call_count += 1;
        return current_status;
    }

    /*! \brief Returns the configured active mono input identity. */
    [[nodiscard]] std::optional<InputDeviceIdentity> currentInputDeviceIdentity() const override
    {
        return current_input_identity;
    }

    /*! \brief Stores a non-owning listener pointer for explicit test notifications. */
    void addListener(IAudioDeviceConfiguration::Listener& listener) override
    {
        listeners.push_back(&listener);
    }

    /*! \brief Removes a listener pointer previously registered with addListener(). */
    void removeListener(IAudioDeviceConfiguration::Listener& listener) override
    {
        std::erase(listeners, &listener);
    }

    /*! \brief Notifies every registered listener that device configuration changed. */
    void notifyChanged()
    {
        for (IAudioDeviceConfiguration::Listener* listener : listeners)
        {
            listener->onAudioDeviceConfigurationChanged();
        }
    }

    /*! \brief JUCE device manager owned by the fake. */
    juce::AudioDeviceManager device_manager{};

    /*! \brief Current device status returned by currentDeviceStatus(). */
    AudioDeviceStatus current_status{};

    /*! \brief Current input identity returned by currentInputDeviceIdentity(). */
    std::optional<InputDeviceIdentity> current_input_identity{};

    /*! \brief Result returned by restoreSerializedDeviceState(). */
    bool restore_serialized_device_state_result{true};

    /*! \brief Optional typed error returned by the next restoreSerializedDeviceState() call. */
    std::optional<AudioDeviceConfigurationError> next_restore_serialized_device_state_error{};

    /*! \brief Current serialized state returned by serializedDeviceState(). */
    std::optional<std::string> serialized_device_state{};

    /*! \brief Last serialized state passed to restoreSerializedDeviceState(). */
    std::optional<std::string> last_restored_serialized_device_state{};

    /*! \brief Non-owning listeners registered by the object under test. */
    std::vector<IAudioDeviceConfiguration::Listener*> listeners{};

    /*! \brief Number of current-device-status reads received. */
    mutable int status_call_count{0};

    /*! \brief Number of serialized restore requests received. */
    int restore_serialized_device_state_call_count{0};

    /*! \brief Number of serialized capture requests received. */
    mutable int serialized_device_state_call_count{0};
};

} // namespace rock_hero::common::audio::testing
