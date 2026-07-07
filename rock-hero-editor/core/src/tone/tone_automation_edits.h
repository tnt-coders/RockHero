/*!
\file tone_automation_edits.h
\brief Undoable editor edit for tone-chain plugin parameter automation, musical positions first.
*/

#pragma once

#include "controller/editor_undo_history.h"

#include <expected>
#include <rock_hero/common/core/tone/tone_automation.h>
#include <string>
#include <utility>
#include <vector>

namespace rock_hero::editor::core
{

/*!
\brief Writes one parameter's automation points into the session model.

Replaces the arrangement entry keyed by (\p plugin_id, \p param_id); an empty point list erases the
entry. The model is the persisted truth; callers rewrite the derived playback curve separately.

\param session Session whose current arrangement owns the automation.
\param plugin_id Durable plugin id keying the entry.
\param param_id Parameter id within the plugin.
\param points Replacement musical points; empty removes the entry.
\return False when no current arrangement exists.
*/
[[nodiscard]] bool applyToneAutomationModel(
    common::core::Session& session, const std::string& plugin_id, const std::string& param_id,
    const std::vector<common::core::ToneAutomationPoint>& points);

/*!
\brief Rewrites the derived playback curve for one parameter from musical points.

Best-effort by design: the model is the truth, so a rewrite that fails (tone not loaded, parameter
unresolved) only logs — the next successful load rebuild reconciles the cache.

\param context Edit context carrying the automation port and session tempo map.
\param tone_document_ref Tone whose chain owns the plugin.
\param instance_id Live plugin instance id.
\param param_id Parameter id within the plugin.
\param points Musical points converted to seconds for the backend curve.
*/
void rewriteDerivedToneCurve(
    EditorEditContext& context, const std::string& tone_document_ref,
    const std::string& instance_id, const std::string& param_id,
    const std::vector<common::core::ToneAutomationPoint>& points);

/*!
\brief Replaces one tone-chain plugin parameter's automation points, undoably.

The memento captures the full musical point list before and after; undo and redo write the model
(the truth) and best-effort rewrite the derived playback curve. No plugin chunk is involved: the
plugin-parameter and tone-automation undo domains are disjoint by design.
*/
struct ToneAutomationPointsEdit final : public IEdit
{
    /*!
    \brief Creates a tone-automation points edit.
    \param plugin_id_value Durable plugin id keying the arrangement entry.
    \param instance_id_value Live plugin instance owning the parameter.
    \param param_id_value Parameter id within the plugin.
    \param tone_document_ref_value Tone whose chain owns the plugin.
    \param param_name_value User-facing parameter name used for the undo label.
    \param before_value Musical points before the edit.
    \param after_value Musical points after the edit.
    */
    ToneAutomationPointsEdit(
        std::string plugin_id_value, std::string instance_id_value, std::string param_id_value,
        std::string tone_document_ref_value, std::string param_name_value,
        std::vector<common::core::ToneAutomationPoint> before_value,
        std::vector<common::core::ToneAutomationPoint> after_value)
        : plugin_id(std::move(plugin_id_value))
        , instance_id(std::move(instance_id_value))
        , param_id(std::move(param_id_value))
        , tone_document_ref(std::move(tone_document_ref_value))
        , param_name(std::move(param_name_value))
        , before(std::move(before_value))
        , after(std::move(after_value))
    {}

    /*! \copydoc IEdit::undo */
    [[nodiscard]] std::expected<void, EditorUndoFailureCode> undo(
        EditorEditContext& context) const override;

    /*! \copydoc IEdit::redo */
    [[nodiscard]] std::expected<void, EditorUndoFailureCode> redo(
        EditorEditContext& context) const override;

    /*! \copydoc IEdit::label */
    [[nodiscard]] std::string label() const override;

    /*! \brief Durable plugin id keying the arrangement entry. */
    std::string plugin_id;

    /*! \brief Live plugin instance owning the parameter. */
    std::string instance_id;

    /*! \brief Parameter id within the plugin. */
    std::string param_id;

    /*! \brief Tone whose chain owns the plugin. */
    std::string tone_document_ref;

    /*! \brief User-facing parameter name used for the undo label. */
    std::string param_name;

    /*! \brief Musical points before the edit. */
    std::vector<common::core::ToneAutomationPoint> before;

    /*! \brief Musical points after the edit. */
    std::vector<common::core::ToneAutomationPoint> after;

private:
    // Writes the supplied points into the model and best-effort rewrites the derived curve.
    [[nodiscard]] std::expected<void, EditorUndoFailureCode> applyPoints(
        EditorEditContext& context,
        const std::vector<common::core::ToneAutomationPoint>& points) const;
};

} // namespace rock_hero::editor::core
