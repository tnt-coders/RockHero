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
#include <rock_hero/common/audio/input/i_live_input.h>
#include <rock_hero/common/audio/input/input_calibration.h>
#include <rock_hero/common/audio/input/input_device_identity.h>
#include <rock_hero/common/audio/input/live_input_monitor.h>
#include <rock_hero/common/audio/input/live_input_monitoring_status.h>
#include <rock_hero/common/audio/settings/i_audio_config_store.h>
#include <rock_hero/game/core/audio/game_audio_config.h>
#include <rock_hero/game/core/settings/i_game_settings.h>
#include <string>

namespace rock_hero::game::core
{

/*!
\brief Observable phase of the native audio-setup flow.

The flow is a strict progression: the player picks a device, the applied route is calibrated for
gain, and the setup reaches Ready — the point at which the game's own store holds the device route
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

    /*! \brief The device route and a matching gain calibration are persisted; the guitar is audible. */
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

    /*! \brief Persisting the device route or the player-slot config failed. */
    StorePersistFailed,

    /*! \brief The gain-calibration measurement or its commit failed; the applied device is intact. */
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

/*! \brief Progress reported while a gain-calibration measurement pass is running. */
enum class GainCalibrationProgress : std::uint8_t
{
    /*! \brief Discarding the first samples after the route was reset. */
    Settling,

    /*! \brief Waiting for the player to strum a usable signal. */
    WaitingForStrum,

    /*! \brief Accumulating the active measurement window. */
    Measuring,

    /*! \brief The measurement completed and its gain was committed; the flow is now Ready. */
    Committed,
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
    \return True in every phase except CalibratingGain, where a measurement must finish or cancel first.
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
shared staged device-settings workflow, captures the applied route (opaque blob plus resolved
identity) into the game's own audio-config store as one ActiveDeviceRoute, writes the slot-0
player-to-route mapping through game/core settings, and then drives the shared calibrate-first
LiveInputMonitor to measure and persist the route's input gain. Reaching Ready is the state a later
GameplaySession Ready transition (plan 14 Phase 4) needs to arm live-input monitoring.

All operations are message-thread operations, matching the ports they drive. The store injected
here must be the same instance the LiveInputMonitor writes calibration through, and the live-input
port the same one that monitor wraps, so the persisted route and calibration land in one store.
*/
class NativeAudioSetup final
{
public:
    /*! \brief Meter-window counts used by one automatic gain-calibration capture pass. */
    struct CaptureSettings
    {
        /*! \brief Number of initial samples discarded after the route is reset. */
        std::size_t settle_sample_count{0};

        /*! \brief Number of quiet samples accepted while waiting for a usable strum. */
        std::size_t wait_sample_count{0};

        /*! \brief Number of active samples used for the measurement window. */
        std::size_t measurement_sample_count{1};
    };

    /*!
    \brief Creates an idle native audio-setup driver over the composed ports.
    \param device_settings Shared staged device-settings workflow the picker drives.
    \param device_configuration Device-configuration port sampled for the applied blob and identity.
    \param live_input_monitor Shared calibrate-first monitor driven to measure and persist gain.
    \param live_input Live-input port sampled for the raw calibration meter level.
    \param audio_config_store The game's own store the applied device route is written to.
    \param game_settings The game's persistence port the slot-0 player config is written to.
    \param capture_settings Fixed meter-window counts for automatic gain capture.
    */
    NativeAudioSetup(
        common::audio::IAudioDeviceSettings& device_settings,
        common::audio::IAudioDeviceConfiguration& device_configuration,
        common::audio::LiveInputMonitor& live_input_monitor, common::audio::ILiveInput& live_input,
        common::audio::IAudioConfigStore& audio_config_store, IGameSettings& game_settings,
        CaptureSettings capture_settings);

    /*! \brief Copying is disabled because the driver holds injected port references. */
    NativeAudioSetup(const NativeAudioSetup&) = delete;

    /*!
    \brief Copy assignment is disabled because the driver holds injected port references.
    \return Reference to this driver.
    */
    NativeAudioSetup& operator=(const NativeAudioSetup&) = delete;

    /*! \brief Moving is disabled so the driver keeps a stable address for its capture pass. */
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
    \brief Applies the staged device route, persists it, and advances to gain calibration.

    On a successful apply the resolved route (opaque restore blob plus mono input identity) is
    captured and written as one ActiveDeviceRoute into the game's store, the slot-0 player-to-route
    mapping is written through game/core settings, and the primary player's route is mirrored into
    ActiveDeviceRoute.identity — all in this one apply so the mirror never drifts from the blob. A
    failed apply, an unresolved route, or a persistence failure is terminal.

    \return Empty success, or the typed reason the apply failed.
    */
    [[nodiscard]] std::expected<void, NativeAudioSetupError> applySelectedDevice();

    /*!
    \brief Begins a gain-calibration measurement on the applied route.

    Drives the shared monitor to open a calibration prompt and start metering the raw input while
    the player strums. Legal only in CalibratingGain.

    \return Empty success, or a typed calibration failure (the applied device stays intact).
    */
    [[nodiscard]] std::expected<void, NativeAudioSetupError> beginGainCalibration();

    /*!
    \brief Feeds one raw input meter sample into the active measurement.

    Reads the live-input port's raw meter level and advances the capture. On completion it commits
    the measured gain through the monitor — which persists the calibration to the game's store — and
    advances to Ready. A signal-quality failure restores the route and stays in CalibratingGain for
    another attempt.

    \return The capture progress, or a typed calibration failure.
    */
    [[nodiscard]] std::expected<GainCalibrationProgress, NativeAudioSetupError>
    sampleGainCalibration();

    /*! \brief Cancels an active measurement, leaving the applied device intact and uncalibrated. */
    void cancelGainCalibration();

private:
    // Builds the monitoring context the shared calibrate-first gate requires to start a measurement.
    [[nodiscard]] common::audio::LiveInputMonitoringContext calibrationContext() const noexcept;

    // Commits a measured gain through the monitor (persisting it) and advances to Ready.
    [[nodiscard]] std::expected<void, NativeAudioSetupError> commitMeasuredGain(double gain_db);

    // Records a terminal failure on the machine and returns the same error for propagation.
    [[nodiscard]] NativeAudioSetupError failAndRecord(NativeAudioSetupError error);

    NativeAudioSetupMachine m_machine;
    common::audio::IAudioDeviceSettings& m_device_settings;
    common::audio::IAudioDeviceConfiguration& m_device_configuration;
    common::audio::LiveInputMonitor& m_live_input_monitor;
    common::audio::ILiveInput& m_live_input;
    common::audio::IAudioConfigStore& m_audio_config_store;
    IGameSettings& m_game_settings;

    // Deterministic capture pass driven one raw meter sample at a time during CalibratingGain.
    common::audio::InputCalibrationCapture m_capture;

    // Route identity resolved at the last successful apply; the expected identity for the commit.
    std::optional<common::audio::InputDeviceIdentity> m_active_route_identity;
};

} // namespace rock_hero::game::core
