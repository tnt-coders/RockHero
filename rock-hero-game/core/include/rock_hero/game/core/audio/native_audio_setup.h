/*!
\file native_audio_setup.h
\brief Headless game native audio-setup driver: device selection then gain calibration.
*/

#pragma once

#include <cstdint>
#include <expected>
#include <optional>
#include <rock_hero/common/audio/device/audio_device_settings.h>
#include <rock_hero/common/audio/device/i_audio_device_configuration.h>
#include <rock_hero/common/audio/input/input_device_identity.h>
#include <rock_hero/common/audio/input/live_input_monitor.h>
#include <rock_hero/common/audio/input/live_input_monitoring_status.h>
#include <rock_hero/game/core/audio/game_audio_config.h>
#include <rock_hero/game/core/settings/i_game_settings.h>
#include <string>

namespace rock_hero::game::core
{

/*!
\brief Observable phase of the native audio-setup flow.

The flow is a strict progression: the player picks a device, the applied route is calibrated for
gain, and the setup reaches Ready — the point at which the shared store holds the device route
and a matching calibration, so a later GameplaySession Ready transition arms live-input monitoring
and the guitar is audible through the tone. A device-apply or persistence failure is terminal
(Failed); a signal-quality calibration failure is recoverable and leaves the flow in CalibratingGain
for another attempt.
*/
enum class NativeAudioSetupPhase : std::uint8_t
{
    /*! \brief No device has been selected yet; the flow has not started. */
    Idle,

    /*! \brief The player is staging a device route through the shared settings workflow. */
    SelectingDevice,

    /*! \brief A device is applied and its route is being calibrated for input gain. */
    CalibratingGain,

    /*!
    \brief The device route and a matching gain calibration are persisted; the guitar is audible.
    */
    Ready,

    /*! \brief A device-apply or persistence step failed; failure() carries the typed reason. */
    Failed,
};

/*! \brief Stable failure codes for native audio-setup operations. */
enum class NativeAudioSetupErrorCode : std::uint8_t
{
    /*! \brief The operation was not valid in the current setup phase. */
    InvalidRequest,

    /*! \brief Applying the staged device route failed. */
    DeviceApplyFailed,

    /*! \brief The applied device did not resolve a usable mono input route to calibrate. */
    DeviceRouteUnresolved,

    /*! \brief Persisting the player-slot config failed. */
    StorePersistFailed,

    /*!
    \brief The measurement could not begin or its gain could not be stored; the applied device is
    intact and the flow stays in CalibratingGain.
    */
    CalibrationFailed,
};

/*! \brief Typed error returned by native audio-setup operations. */
struct [[nodiscard]] NativeAudioSetupError
{
    /*! \brief Stable error code used by callers for branching. */
    NativeAudioSetupErrorCode code{};

    /*! \brief User-facing or diagnostic error message. */
    std::string message;

    /*!
    \brief Creates an error with the default message for its code.
    \param error_code Stable error code used by callers for branching.
    */
    explicit NativeAudioSetupError(NativeAudioSetupErrorCode error_code);

    /*!
    \brief Creates an error with operation-specific detail.
    \param error_code Stable error code used by callers for branching.
    \param message_text User-facing or diagnostic error message.
    */
    NativeAudioSetupError(NativeAudioSetupErrorCode error_code, std::string message_text);
};

/*!
\brief Pure phase machine for the native audio-setup flow.

Holds only the observable phase and the terminal failure, enforcing the legal progression so the
driver's side effects can never leave an illegal state. Per docs/design/architectural-principles.md
"Separate State From Side Effects", this type touches no audio ports; the NativeAudioSetup adapter
owns the ports and feeds outcomes here.
*/
class NativeAudioSetupMachine final
{
public:
    /*!
    \brief Returns the current setup phase.
    \return Current phase.
    */
    [[nodiscard]] NativeAudioSetupPhase phase() const noexcept;

    /*!
    \brief Returns the failure that moved the flow to Failed.
    \return The typed error while Failed, or empty in every other phase.
    */
    [[nodiscard]] const std::optional<NativeAudioSetupError>& failure() const noexcept;

    /*!
    \brief Reports whether a device apply may begin from the current phase.
    \return True in every phase except CalibratingGain, where a
            measurement must finish or cancel first.
    */
    [[nodiscard]] bool canApplyDevice() const noexcept;

    /*!
    \brief Reports whether gain calibration may run from the current phase.
    \return True only in CalibratingGain.
    */
    [[nodiscard]] bool canCalibrate() const noexcept;

    /*! \brief Enters device selection, clearing any prior terminal failure. */
    void beginDeviceSelection() noexcept;

    /*! \brief Records a successful device apply, advancing selection to gain calibration. */
    void deviceApplied() noexcept;

    /*! \brief Records a committed gain calibration, advancing to Ready. */
    void calibrationCommitted() noexcept;

    /*!
    \brief Moves the flow to the terminal Failed phase.
    \param error Typed reason to expose through failure().
    */
    void fail(NativeAudioSetupError error) noexcept;

private:
    NativeAudioSetupPhase m_phase{NativeAudioSetupPhase::Idle};
    std::optional<NativeAudioSetupError> m_failure;
};

/*!
\brief Headless adapter sequencing device selection then gain calibration for the game.

The driver is the thin side-effecting adapter over a pure NativeAudioSetupMachine: it drives the
shared staged device-settings workflow (whose apply saves the route in the shared audio-config
store), writes the resolved input identity as the slot-0 player-to-route mapping through game/core
settings, and then drives the shared calibrate-first LiveInputMonitor to measure and persist the
route's input gain. Reaching Ready is the state a later GameplaySession Ready transition (plan 14
Phase 4) needs to arm live-input monitoring.

All operations are message-thread operations, matching the ports they drive.
*/
class NativeAudioSetup final
{
public:
    /*!
    \brief Creates an idle native audio-setup driver over the composed ports.
    \param device_settings Shared staged device-settings workflow the picker drives.
    \param device_configuration Device-configuration port sampled for the applied blob and identity.
    \param live_input_monitor Shared calibrate-first monitor that measures and stores the gain.
    \param game_settings The game's persistence port the slot-0 player config is written to.
    */
    NativeAudioSetup(
        common::audio::IAudioDeviceSettings& device_settings,
        common::audio::IAudioDeviceConfiguration& device_configuration,
        common::audio::LiveInputMonitor& live_input_monitor, IGameSettings& game_settings);

    /*! \brief Copying is disabled because the driver holds injected port references. */
    NativeAudioSetup(const NativeAudioSetup&) = delete;

    /*!
    \brief Copy assignment is disabled because the driver holds injected port references.
    \return Reference to this driver.
    */
    NativeAudioSetup& operator=(const NativeAudioSetup&) = delete;

    /*! \brief Moving is disabled so the driver keeps a stable address. */
    NativeAudioSetup(NativeAudioSetup&&) = delete;

    /*!
    \brief Move assignment is disabled so the driver keeps a stable address.
    \return Reference to this driver.
    */
    NativeAudioSetup& operator=(NativeAudioSetup&&) = delete;

    /*! \brief Destroys the driver. */
    ~NativeAudioSetup() = default;

    /*!
    \brief Returns the current setup phase.
    \return Current phase.
    */
    [[nodiscard]] NativeAudioSetupPhase phase() const noexcept;

    /*!
    \brief Returns the failure that moved the flow to Failed.
    \return The typed error while Failed, or empty in every other phase.
    */
    [[nodiscard]] std::optional<NativeAudioSetupError> failure() const;

    /*! \brief Enters device selection so the picker can stage a route; no-op mid-calibration. */
    void beginDeviceSelection() noexcept;

    /*!
    \brief Returns the staged device-settings workflow the picker drives.
    \return Reference to the shared device-settings workflow.
    */
    [[nodiscard]] common::audio::IAudioDeviceSettings& deviceSettings() noexcept;

    /*!
    \brief Applies the staged device route and advances to gain calibration.

    The apply saves the route in the shared audio-config store; the resolved mono input identity
    then becomes the slot-0 player-to-route mapping written through game/core settings. A failed
    apply, an unresolved route, or a persistence failure is terminal.

    \return Empty success, or the typed reason the apply failed.
    */
    [[nodiscard]] std::expected<void, NativeAudioSetupError> applySelectedDevice();

    /*!
    \brief Begins a gain-calibration measurement on the applied route.

    Hands the route to the shared monitor's measurement; the caller then samples it at
    common::audio::inputCalibrationSampleRateHz() while the player strums. Legal only in
    CalibratingGain.

    \param pickups The pickups the player measures with; there is no default, since the wizard
           must ask.
    \return Empty success, or a typed calibration failure (the applied device stays intact).
    */
    [[nodiscard]] std::expected<void, NativeAudioSetupError> beginGainCalibration(
        common::audio::PickupClass pickups);

    /*!
    \brief Samples the measurement once.

    A finished measurement's gain is stored in the shared store at once, and the flow advances to
    Ready. A failed one leaves the flow in CalibratingGain for another attempt.

    \return The measurement's progress (a failed measurement is a progress value, recoverable by
            beginning another), InvalidRequest when no measurement was begun, or CalibrationFailed
            when the measured gain could not be stored.
    */
    [[nodiscard]] std::expected<common::audio::InputCalibrationProgress, NativeAudioSetupError>
    sampleGainCalibration();

    /*! \brief Cancels an active measurement, leaving the applied device intact and uncalibrated. */
    void cancelGainCalibration();

private:
    // Records a terminal failure on the machine and returns the same error for propagation.
    [[nodiscard]] NativeAudioSetupError failAndRecord(NativeAudioSetupError error);

    NativeAudioSetupMachine m_machine;
    common::audio::IAudioDeviceSettings& m_device_settings;
    common::audio::IAudioDeviceConfiguration& m_device_configuration;
    common::audio::LiveInputMonitor& m_live_input_monitor;
    IGameSettings& m_game_settings;
};

} // namespace rock_hero::game::core
