/*!
\file fake_live_input.h
\brief Ordered-recording live-input test implementation.
*/

#pragma once

#include <compare>
#include <cstdint>
#include <expected>
#include <optional>
#include <ostream>
#include <rock_hero/common/audio/input/audio_meter_snapshot.h>
#include <rock_hero/common/audio/input/i_live_input.h>
#include <rock_hero/common/audio/input/live_input_error.h>
#include <rock_hero/common/audio/shared/gain.h>
#include <vector>

namespace rock_hero::common::audio::testing
{

/*! \brief Identifies which ILiveInput mutating setter produced a recorded call. */
enum class LiveInputSetter : std::uint8_t
{
    /*! \brief setInputGain(Gain). */
    InputGain,

    /*! \brief setLiveInputMonitoringEnabled(bool). */
    LiveInputMonitoring,

    /*! \brief setCalibrationInputMonitoringEnabled(bool). */
    CalibrationInputMonitoring,
};

/*!
\brief One recorded ILiveInput setter invocation, ordered across all three setters.

The trace records every invocation, including ones that return an injected failure, so the ordered
sequence characterizes the exact port-driving contract independent of which store or error type a
later refactor uses.
*/
struct LiveInputSetterCall
{
    /*! \brief Which setter was invoked. */
    LiveInputSetter setter{};

    /*! \brief Requested gain in decibels; meaningful only for LiveInputSetter::InputGain. */
    double gain_db{0.0};

    /*! \brief Requested enable flag; meaningful only for the two monitoring setters. */
    bool enabled{false};

    /*!
    \brief Compares two recorded calls field by field.
    \param lhs Left-hand recorded call.
    \param rhs Right-hand recorded call.
    \return True when the setter, gain, and enable flag all match.
    */
    friend bool operator==(const LiveInputSetterCall& lhs, const LiveInputSetterCall& rhs) noexcept
    {
        // Hand-written, not defaulted: a defaulted comparison trips clang's -Wfloat-equal on
        // gain_db. The ordering query expresses exact equality warning-free (see Gain::operator==).
        return lhs.setter == rhs.setter && lhs.enabled == rhs.enabled &&
               std::is_eq(lhs.gain_db <=> rhs.gain_db);
    }
};

/*!
\brief Builds an expected setInputGain trace entry for golden-trace assertions.
\param gain_db Requested gain in decibels.
\return Recorded-call value describing a setInputGain invocation.
*/
[[nodiscard]] inline LiveInputSetterCall setInputGainCall(double gain_db)
{
    return LiveInputSetterCall{.setter = LiveInputSetter::InputGain, .gain_db = gain_db};
}

/*!
\brief Builds an expected setLiveInputMonitoringEnabled trace entry for golden-trace assertions.
\param enabled Requested live-monitoring enable flag.
\return Recorded-call value describing a setLiveInputMonitoringEnabled invocation.
*/
[[nodiscard]] inline LiveInputSetterCall setLiveInputMonitoringCall(bool enabled)
{
    return LiveInputSetterCall{.setter = LiveInputSetter::LiveInputMonitoring, .enabled = enabled};
}

/*!
\brief Builds an expected setCalibrationInputMonitoringEnabled trace entry for assertions.
\param enabled Requested calibration-monitoring enable flag.
\return Recorded-call value describing a setCalibrationInputMonitoringEnabled invocation.
*/
[[nodiscard]] inline LiveInputSetterCall setCalibrationInputMonitoringCall(bool enabled)
{
    return LiveInputSetterCall{
        .setter = LiveInputSetter::CalibrationInputMonitoring, .enabled = enabled
    };
}

/*!
\brief Streams a recorded call so Catch2 renders trace mismatches legibly.
\param stream Output stream to write to.
\param call Recorded call to render.
\return The same output stream.
*/
inline std::ostream& operator<<(std::ostream& stream, const LiveInputSetterCall& call)
{
    switch (call.setter)
    {
        case LiveInputSetter::InputGain:
            return stream << "SG(" << call.gain_db << ")";
        case LiveInputSetter::LiveInputMonitoring:
            return stream << "SL(" << (call.enabled ? "true" : "false") << ")";
        case LiveInputSetter::CalibrationInputMonitoring:
            return stream << "SC(" << (call.enabled ? "true" : "false") << ")";
    }
    return stream << "SUnknown";
}

/*!
\brief ILiveInput implementation that records the ordered setter call sequence.

Use this when a test must characterize the exact live-input port-driving contract: the order across
setInputGain, setLiveInputMonitoringEnabled, and setCalibrationInputMonitoringEnabled, and whether a
given invocation failed. It mirrors the state-tracking of the shared transport fake (current gain,
monitoring flags) so route snapshots and best-effort rollbacks behave as the production backend
would, while adding the cross-setter ordered trace the transport fake lacks. Inject a one-shot
failure through the matching next_set_*_error member; the failing call is still recorded in the
trace, matching the production backend's "one setter call regardless of success" contract.
*/
class FakeLiveInput final : public ILiveInput
{
public:
    /*!
    \brief Returns the current input gain and records the read.
    \return Current input gain applied before the signal chain.
    */
    [[nodiscard]] Gain inputGain() const override
    {
        input_gain_read_count += 1;
        return current_input_gain;
    }

    /*!
    \brief Records a setInputGain call, then applies the gain unless a failure is injected.
    \param gain Desired input gain.
    \return Empty success, or the injected one-shot failure.
    */
    [[nodiscard]] std::expected<void, LiveInputError> setInputGain(Gain gain) override
    {
        calls.push_back(setInputGainCall(gain.db));
        set_input_gain_call_count += 1;
        if (next_set_input_gain_error.has_value())
        {
            LiveInputError error = std::move(*next_set_input_gain_error);
            next_set_input_gain_error.reset();
            return std::unexpected{std::move(error)};
        }

        current_input_gain = clampGain(gain);
        return {};
    }

    /*!
    \brief Returns the configured raw input meter level.
    \return Latest raw input meter level.
    */
    [[nodiscard]] AudioMeterLevel rawInputMeterLevel() const override
    {
        return raw_input_meter_level;
    }

    /*!
    \brief Returns whether processed live monitoring is enabled and records the read.
    \return True when live monitoring is routed through the signal chain.
    */
    [[nodiscard]] bool liveInputMonitoringEnabled() const override
    {
        live_input_monitoring_read_count += 1;
        return live_input_monitoring_enabled;
    }

    /*!
    \brief Records a setLiveInputMonitoringEnabled call, then applies it unless a failure is injected.
    \param enabled True to route calibrated live guitar through the chain.
    \return Empty success, or the injected one-shot failure.
    */
    [[nodiscard]] std::expected<void, LiveInputError> setLiveInputMonitoringEnabled(
        bool enabled) override
    {
        calls.push_back(setLiveInputMonitoringCall(enabled));
        set_live_input_monitoring_call_count += 1;
        if (next_set_live_input_monitoring_error.has_value())
        {
            LiveInputError error = std::move(*next_set_live_input_monitoring_error);
            next_set_live_input_monitoring_error.reset();
            return std::unexpected{std::move(error)};
        }

        live_input_monitoring_enabled = enabled;
        return {};
    }

    /*!
    \brief Returns whether calibration monitoring is enabled and records the read.
    \return True when live guitar is routed directly to output for calibration.
    */
    [[nodiscard]] bool calibrationInputMonitoringEnabled() const override
    {
        calibration_input_monitoring_read_count += 1;
        return calibration_input_monitoring_enabled;
    }

    /*!
    \brief Records a setCalibrationInputMonitoringEnabled call, then applies it unless a failure is
    injected.
    \param enabled True to route live guitar directly to output for calibration.
    \return Empty success, or the injected one-shot failure.
    */
    [[nodiscard]] std::expected<void, LiveInputError> setCalibrationInputMonitoringEnabled(
        bool enabled) override
    {
        calls.push_back(setCalibrationInputMonitoringCall(enabled));
        set_calibration_input_monitoring_call_count += 1;
        if (next_set_calibration_input_monitoring_error.has_value())
        {
            LiveInputError error = std::move(*next_set_calibration_input_monitoring_error);
            next_set_calibration_input_monitoring_error.reset();
            return std::unexpected{std::move(error)};
        }

        calibration_input_monitoring_enabled = enabled;
        return {};
    }

    /*! \brief Ordered record of every setter invocation across all three setters. */
    std::vector<LiveInputSetterCall> calls{};

    /*! \brief Current input gain returned by inputGain(). */
    Gain current_input_gain{};

    /*! \brief Raw input meter level returned by rawInputMeterLevel(). */
    AudioMeterLevel raw_input_meter_level{};

    /*! \brief Current processed-monitoring flag returned by liveInputMonitoringEnabled(). */
    bool live_input_monitoring_enabled{false};

    /*! \brief Current calibration-monitoring flag returned by calibrationInputMonitoringEnabled(). */
    bool calibration_input_monitoring_enabled{false};

    /*! \brief One-shot failure injected before the next setInputGain applies its value. */
    std::optional<LiveInputError> next_set_input_gain_error{};

    /*! \brief One-shot failure injected before the next setLiveInputMonitoringEnabled applies. */
    std::optional<LiveInputError> next_set_live_input_monitoring_error{};

    /*! \brief One-shot failure injected before the next setCalibrationInputMonitoringEnabled applies. */
    std::optional<LiveInputError> next_set_calibration_input_monitoring_error{};

    /*! \brief Number of setInputGain invocations recorded, including failures. */
    int set_input_gain_call_count{0};

    /*! \brief Number of setLiveInputMonitoringEnabled invocations recorded, including failures. */
    int set_live_input_monitoring_call_count{0};

    /*! \brief Number of setCalibrationInputMonitoringEnabled invocations recorded, including failures. */
    int set_calibration_input_monitoring_call_count{0};

    /*! \brief Number of inputGain() reads observed by the fake. */
    mutable int input_gain_read_count{0};

    /*! \brief Number of liveInputMonitoringEnabled() reads observed by the fake. */
    mutable int live_input_monitoring_read_count{0};

    /*! \brief Number of calibrationInputMonitoringEnabled() reads observed by the fake. */
    mutable int calibration_input_monitoring_read_count{0};
};

} // namespace rock_hero::common::audio::testing
