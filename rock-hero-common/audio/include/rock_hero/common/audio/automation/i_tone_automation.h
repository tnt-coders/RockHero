/*!
\file i_tone_automation.h
\brief Tracktion-free port for authoring plugin-parameter automation within a tone's chain.
*/

#pragma once

#include <expected>
#include <rock_hero/common/audio/automation/tone_automation_error.h>
#include <span>
#include <string>
#include <vector>

namespace rock_hero::common::audio
{

/*!
\brief One automatable parameter exposed by a plugin in a tone's signal chain.

A parameter is identified by the pair (\ref instance_id, \ref param_id): \ref param_id is unique
only within one plugin, so both are needed to address a lane. Values are always normalised to
`[0, 1]`, independent of the parameter's native range.
*/
struct [[nodiscard]] AutomatableParamInfo
{
    /*! \brief Owning plugin instance id (matches the plugin chain snapshot's instance ids). */
    std::string instance_id;

    /*! \brief Parameter id, stable per plugin (Tracktion \c paramID). */
    std::string param_id;

    /*! \brief User-facing parameter name. */
    std::string name;

    /*! \brief Group/submenu name for the picker, empty when the parameter is ungrouped. */
    std::string group;

    /*! \brief Whether the parameter is stepped rather than continuous. */
    bool is_discrete{false};

    /*! \brief Ordered step labels for a discrete parameter; empty for a continuous one. */
    std::vector<std::string> labels;

    /*! \brief Default parameter value, normalised to `[0, 1]`. */
    float default_norm_value{0.0F};
};

/*!
\brief One automation curve point in edit-timeline seconds with a normalised value.

\ref seconds is an absolute edit-timeline position; editor-core converts to and from musical
positions through the song tempo map. \ref norm_value is normalised to `[0, 1]`. \ref curve_shape
is the segment shape toward the next point, in `[-1, 1]` (0 is linear).
*/
struct [[nodiscard]] AutomationCurvePoint
{
    /*! \brief Edit-timeline position in seconds. */
    double seconds{0.0};

    /*! \brief Parameter value normalised to `[0, 1]`. */
    float norm_value{0.0F};

    /*! \brief Segment shape toward the next point, in `[-1, 1]`; 0 is linear. */
    float curve_shape{0.0F};
};

/*!
\brief Project-owned facade for reading and writing plugin-parameter automation in a tone chain.

All methods are message-thread operations against the currently loaded live rig. Curves are the
base per-parameter automation curves that Tracktion evaluates during playback; RockHero owns undo
through its own point-list mementos, so every write passes a null undo manager to the backend.
*/
class IToneAutomation
{
public:
    /*! \brief Destroys the tone automation interface. */
    virtual ~IToneAutomation() = default;

    /*!
    \brief Lists the automatable parameters of every plugin in a loaded tone's chain.

    Parameters are returned grouped (the group name populates \ref AutomatableParamInfo::group), with
    the synthetic dry/wet mix parameters filtered out.

    \param tone_document_ref One of the tone references currently loaded into the live rig.
    \return The tone's automatable parameters, or a typed failure when the tone is not loaded.
    */
    [[nodiscard]] virtual std::expected<std::vector<AutomatableParamInfo>, ToneAutomationError>
    listAutomatableParameters(const std::string& tone_document_ref) const = 0;

    /*!
    \brief Reads the current automation curve points for one parameter.

    \param tone_document_ref One of the tone references currently loaded into the live rig.
    \param instance_id Plugin instance whose parameter is read.
    \param param_id Parameter id within that plugin.
    \return The curve points in ascending time (empty when the parameter has no curve), or a typed
            failure when the tone, plugin, or parameter cannot be resolved.
    */
    [[nodiscard]] virtual std::expected<std::vector<AutomationCurvePoint>, ToneAutomationError>
    readParameterCurve(
        const std::string& tone_document_ref, const std::string& instance_id,
        const std::string& param_id) const = 0;

    /*!
    \brief Replaces one parameter's automation curve with the supplied points.

    The existing curve is cleared and rebuilt from \p points; passing an empty span removes the
    curve. Points must be in ascending time. Editing while the transport plays is safe.

    \param tone_document_ref One of the tone references currently loaded into the live rig.
    \param instance_id Plugin instance whose parameter is written.
    \param param_id Parameter id within that plugin.
    \param points Replacement curve points, normalised value, in ascending time.
    \return Empty success, or a typed failure when the tone, plugin, or parameter cannot be resolved.
    */
    [[nodiscard]] virtual std::expected<void, ToneAutomationError> writeParameterCurve(
        const std::string& tone_document_ref, const std::string& instance_id,
        const std::string& param_id, std::span<const AutomationCurvePoint> points) = 0;
};

} // namespace rock_hero::common::audio
