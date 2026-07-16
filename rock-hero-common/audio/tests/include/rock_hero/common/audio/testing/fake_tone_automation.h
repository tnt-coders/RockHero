/*!
\file fake_tone_automation.h
\brief Recording tone-automation test implementation for derived-curve write coverage.
*/

#pragma once

#include <expected>
#include <rock_hero/common/audio/automation/i_tone_automation.h>
#include <rock_hero/common/audio/automation/tone_automation_error.h>
#include <span>
#include <string>
#include <vector>

namespace rock_hero::common::audio::testing
{

/*! \brief One recorded writeParameterCurve invocation. */
struct ToneAutomationWriteCall
{
    /*! \brief Tone document reference the curve was written against. */
    std::string tone_document_ref;

    /*! \brief Plugin instance id the curve was written against. */
    std::string instance_id;

    /*! \brief Parameter id within the plugin. */
    std::string param_id;

    /*! \brief Replacement curve points as handed to the port. */
    std::vector<AutomationCurvePoint> points;
};

/*!
\brief Records derived-curve writes; the authoring-only read surfaces return trivial values.

writeParameterCurve appends to \ref write_calls in call order. Setting \ref fail_writes_for_param
injects a typed failure for that parameter id while still recording the call, so best-effort
callers can be proven to continue past individual failures.
*/
class FakeToneAutomation final : public IToneAutomation
{
public:
    /*!
    \brief Returns an empty parameter listing (never driven by curve-rebuild callers).
    \param tone_document_ref Ignored.
    \return An empty parameter list.
    */
    [[nodiscard]] std::expected<std::vector<AutomatableParamInfo>, ToneAutomationError>
    listAutomatableParameters(const std::string& tone_document_ref) const override
    {
        (void)tone_document_ref;
        return std::vector<AutomatableParamInfo>{};
    }

    /*!
    \brief Returns an empty curve (curve-rebuild callers never read back).
    \param tone_document_ref Ignored.
    \param instance_id Ignored.
    \param param_id Ignored.
    \return An empty curve.
    */
    [[nodiscard]] std::expected<std::vector<AutomationCurvePoint>, ToneAutomationError>
    readParameterCurve(
        const std::string& tone_document_ref, const std::string& instance_id,
        const std::string& param_id) const override
    {
        (void)tone_document_ref;
        (void)instance_id;
        (void)param_id;
        return std::vector<AutomationCurvePoint>{};
    }

    /*!
    \brief Records the write, failing it when the parameter id matches the injected failure.
    \param tone_document_ref Tone document reference recorded with the call.
    \param instance_id Plugin instance id recorded with the call.
    \param param_id Parameter id recorded with the call.
    \param points Replacement curve points recorded with the call.
    \return Empty success, or the injected typed failure.
    */
    [[nodiscard]] std::expected<void, ToneAutomationError> writeParameterCurve(
        const std::string& tone_document_ref, const std::string& instance_id,
        const std::string& param_id, std::span<const AutomationCurvePoint> points) override
    {
        write_calls.push_back(
            ToneAutomationWriteCall{
                .tone_document_ref = tone_document_ref,
                .instance_id = instance_id,
                .param_id = param_id,
                .points = {points.begin(), points.end()},
            });
        if (param_id == fail_writes_for_param)
        {
            return std::unexpected{ToneAutomationError{
                ToneAutomationErrorCode::ParameterNotFound, "injected write failure"
            }};
        }
        return {};
    }

    /*!
    \brief Returns a fixed live value (never driven by curve-rebuild callers).
    \param tone_document_ref Ignored.
    \param instance_id Ignored.
    \param param_id Ignored.
    \return Zero.
    */
    [[nodiscard]] std::expected<float, ToneAutomationError> readParameterNormValue(
        const std::string& tone_document_ref, const std::string& instance_id,
        const std::string& param_id) const override
    {
        (void)tone_document_ref;
        (void)instance_id;
        (void)param_id;
        return 0.0F;
    }

    /*!
    \brief Returns empty display text (never driven by curve-rebuild callers).
    \param tone_document_ref Ignored.
    \param instance_id Ignored.
    \param param_id Ignored.
    \param norm_value Ignored.
    \return An empty string.
    */
    [[nodiscard]] std::expected<std::string, ToneAutomationError> formatParameterValue(
        const std::string& tone_document_ref, const std::string& instance_id,
        const std::string& param_id, float norm_value) const override
    {
        (void)tone_document_ref;
        (void)instance_id;
        (void)param_id;
        (void)norm_value;
        return std::string{};
    }

    /*!
    \brief Returns a fixed parsed value (never driven by curve-rebuild callers).
    \param tone_document_ref Ignored.
    \param instance_id Ignored.
    \param param_id Ignored.
    \param text Ignored.
    \return Zero.
    */
    [[nodiscard]] std::expected<float, ToneAutomationError> parseParameterValue(
        const std::string& tone_document_ref, const std::string& instance_id,
        const std::string& param_id, const std::string& text) const override
    {
        (void)tone_document_ref;
        (void)instance_id;
        (void)param_id;
        (void)text;
        return 0.0F;
    }

    /*! \brief Derived-curve writes observed, in call order (failed writes included). */
    std::vector<ToneAutomationWriteCall> write_calls{};

    /*! \brief Parameter id whose writes fail with an injected error; empty injects nothing. */
    std::string fail_writes_for_param{};
};

} // namespace rock_hero::common::audio::testing
