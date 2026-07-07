/*!
\file tone_automation_edits.h
\brief Undoable editor edit for tone-chain plugin parameter automation curves.
*/

#pragma once

#include "controller/editor_undo_history.h"

#include <expected>
#include <rock_hero/common/audio/automation/i_tone_automation.h>
#include <string>
#include <utility>
#include <vector>

namespace rock_hero::editor::core
{

/*!
\brief Replaces one tone-chain plugin parameter's automation curve, undoably.

Captures the parameter's full point list before and after the edit, so undo and redo simply rewrite
the whole curve through the audio automation port. Points are seconds-based and normalised, matching
the port; the curve is the authoritative store, so the memento carries no plugin chunk.
*/
struct ToneAutomationPointsEdit final : public IEdit
{
    /*!
    \brief Creates a tone-automation points edit.
    \param tone_document_ref_value Tone whose plugin parameter was edited.
    \param instance_id_value Plugin instance owning the parameter.
    \param param_id_value Parameter id within the plugin.
    \param param_name_value User-facing parameter name used for the undo label.
    \param before_value Curve points before the edit.
    \param after_value Curve points after the edit.
    */
    ToneAutomationPointsEdit(
        std::string tone_document_ref_value, std::string instance_id_value,
        std::string param_id_value, std::string param_name_value,
        std::vector<common::audio::AutomationCurvePoint> before_value,
        std::vector<common::audio::AutomationCurvePoint> after_value)
        : tone_document_ref(std::move(tone_document_ref_value))
        , instance_id(std::move(instance_id_value))
        , param_id(std::move(param_id_value))
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

    /*! \brief Tone whose plugin parameter was edited. */
    std::string tone_document_ref;

    /*! \brief Plugin instance owning the parameter. */
    std::string instance_id;

    /*! \brief Parameter id within the plugin. */
    std::string param_id;

    /*! \brief User-facing parameter name used for the undo label. */
    std::string param_name;

    /*! \brief Curve points before the edit. */
    std::vector<common::audio::AutomationCurvePoint> before;

    /*! \brief Curve points after the edit. */
    std::vector<common::audio::AutomationCurvePoint> after;

private:
    // Rewrites the whole curve to the supplied points through the automation port.
    [[nodiscard]] std::expected<void, EditorUndoFailureCode> applyPoints(
        EditorEditContext& context,
        const std::vector<common::audio::AutomationCurvePoint>& points) const;
};

} // namespace rock_hero::editor::core
