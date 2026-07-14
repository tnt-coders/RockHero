/*!
\file in_memory_audio_config_store.h
\brief In-memory IAudioConfigStore test implementation with read and write support.
*/

#pragma once

#include <algorithm>
#include <expected>
#include <optional>
#include <rock_hero/common/audio/input/input_calibration_state.h>
#include <rock_hero/common/audio/input/input_device_identity.h>
#include <rock_hero/common/audio/settings/active_device_route.h>
#include <rock_hero/common/audio/settings/audio_config_error.h>
#include <rock_hero/common/audio/settings/i_audio_config_store.h>
#include <rock_hero/common/audio/shared/gain.h>
#include <utility>
#include <vector>

namespace rock_hero::common::audio::testing
{

/*!
\brief IAudioConfigStore implementation that keeps the active route and calibration in memory.

Use this when a test needs a fully readable and writable audio-config store without touching disk:
controller device-route restore/persist, migration, and any consumer of the port can round-trip
state through it. It mirrors the concrete store's validity posture (identity validation, gain
clamping, physical-route dedup, empty-blob-clears) so the fake and the JUCE-backed store behave
alike. Each next_*_error member injects one typed failure into the next matching call, following the
FakeLiveInput one-shot-failure pattern, so tests can exercise the typed-failure branches.
*/
class InMemoryAudioConfigStore final : public IAudioConfigStore
{
public:
    /*!
    \brief Returns the stored active device route.
    \return Active device route, or empty when none is stored.
    */
    [[nodiscard]] std::optional<ActiveDeviceRoute> activeDeviceRoute() const override
    {
        return active_device_route;
    }

    /*!
    \brief Stores or clears the active device route, mirroring the concrete empty-blob-clears rule.
    \param route Route to store, or empty to clear the stored route.
    \return Empty success, or the injected one-shot failure.
    */
    [[nodiscard]] std::expected<void, AudioConfigError> setActiveDeviceRoute(
        std::optional<ActiveDeviceRoute> route) override
    {
        if (next_set_active_device_route_error.has_value())
        {
            AudioConfigError error = std::move(*next_set_active_device_route_error);
            next_set_active_device_route_error.reset();
            return std::unexpected{std::move(error)};
        }

        if (!route.has_value() || route->serialized_state.empty())
        {
            active_device_route.reset();
        }
        else
        {
            active_device_route = std::move(route);
        }

        return {};
    }

    /*!
    \brief Reads app-local input calibration for one physical input route.
    \param identity Physical input route to look up.
    \return Calibration state, absence, or the injected one-shot failure.
    */
    [[nodiscard]] std::expected<std::optional<InputCalibrationState>, AudioConfigError>
    inputCalibrationFor(const InputDeviceIdentity& identity) const override
    {
        if (next_input_calibration_for_error.has_value())
        {
            AudioConfigError error = std::move(*next_input_calibration_for_error);
            next_input_calibration_for_error.reset();
            return std::unexpected{std::move(error)};
        }

        if (!isValidInputDeviceIdentity(identity))
        {
            return std::nullopt;
        }

        const auto found = std::ranges::find_if(
            input_calibrations, [&identity](const InputCalibrationState& state) {
                return inputCalibrationMatchesPhysicalRoute(state, identity);
            });
        if (found == input_calibrations.end())
        {
            return std::nullopt;
        }

        InputCalibrationState calibration = *found;
        calibration.input_device_identity = identity;
        return calibration;
    }

    /*!
    \brief Stores or replaces app-local input calibration for its physical route.
    \param calibration_state Calibration state to save.
    \return Empty success, an invalid-route failure, or the injected one-shot failure.
    */
    [[nodiscard]] std::expected<void, AudioConfigError> saveInputCalibration(
        InputCalibrationState calibration_state) override
    {
        if (next_save_input_calibration_error.has_value())
        {
            AudioConfigError error = std::move(*next_save_input_calibration_error);
            next_save_input_calibration_error.reset();
            return std::unexpected{std::move(error)};
        }

        if (!isValidInputDeviceIdentity(calibration_state.input_device_identity))
        {
            return std::unexpected{AudioConfigError{
                AudioConfigErrorCode::InvalidSettingValue,
                "Cannot save input calibration for an invalid input route."
            }};
        }

        calibration_state.calibration_gain = clampGain(calibration_state.calibration_gain);
        std::erase_if(input_calibrations, [&calibration_state](const InputCalibrationState& state) {
            return samePhysicalInputRoute(
                state.input_device_identity, calibration_state.input_device_identity);
        });
        input_calibrations.push_back(std::move(calibration_state));
        return {};
    }

    /*!
    \brief Removes app-local input calibration for one physical input route.
    \param identity Physical input route to remove.
    \return Empty success, an invalid-route failure, or the injected one-shot failure.
    */
    [[nodiscard]] std::expected<void, AudioConfigError> removeInputCalibration(
        const InputDeviceIdentity& identity) override
    {
        if (next_remove_input_calibration_error.has_value())
        {
            AudioConfigError error = std::move(*next_remove_input_calibration_error);
            next_remove_input_calibration_error.reset();
            return std::unexpected{std::move(error)};
        }

        if (!isValidInputDeviceIdentity(identity))
        {
            return std::unexpected{AudioConfigError{
                AudioConfigErrorCode::InvalidSettingValue,
                "Cannot remove input calibration for an invalid input route."
            }};
        }

        std::erase_if(input_calibrations, [&identity](const InputCalibrationState& state) {
            return inputCalibrationMatchesPhysicalRoute(state, identity);
        });
        return {};
    }

    /*! \brief Active device route stored by the fake, or empty when none is set. */
    std::optional<ActiveDeviceRoute> active_device_route{};

    /*! \brief Route-keyed calibration records held by the fake. */
    std::vector<InputCalibrationState> input_calibrations{};

    /*! \brief One-shot failure injected before the next setActiveDeviceRoute stores its value. */
    std::optional<AudioConfigError> next_set_active_device_route_error{};

    /*! \brief One-shot failure injected before the next inputCalibrationFor read. */
    mutable std::optional<AudioConfigError> next_input_calibration_for_error{};

    /*! \brief One-shot failure injected before the next saveInputCalibration stores its value. */
    std::optional<AudioConfigError> next_save_input_calibration_error{};

    /*! \brief One-shot failure injected before the next removeInputCalibration erases its value. */
    std::optional<AudioConfigError> next_remove_input_calibration_error{};
};

} // namespace rock_hero::common::audio::testing
