/*!
\file tone_region_edits.h
\brief Concrete tone-track undo edit objects applied through the editor undo history.
*/

#pragma once

#include "controller/editor_undo_history.h"

#include <expected>
#include <rock_hero/common/core/tone/tone_track.h>
#include <string>
#include <utility>

namespace rock_hero::editor::core
{

/*! \brief Inverse-command edit that restores a tone region's previous musical endpoints. */
struct [[nodiscard]] ToneRegionResizeEdit final : IEdit
{
    /*!
    \brief Captures the endpoint change applied by a tone-region resize.
    \param region_id_value Stable id of the resized region.
    \param region_name_value User-facing region name used for the undo label.
    \param before_start_value Musical start before the resize.
    \param before_end_value Musical end before the resize.
    \param after_start_value Musical start after the resize.
    \param after_end_value Musical end after the resize.
    */
    ToneRegionResizeEdit(
        std::string region_id_value, std::string region_name_value,
        common::core::ToneGridPosition before_start_value,
        common::core::ToneGridPosition before_end_value,
        common::core::ToneGridPosition after_start_value,
        common::core::ToneGridPosition after_end_value)
        : region_id(std::move(region_id_value))
        , region_name(std::move(region_name_value))
        , before_start(before_start_value)
        , before_end(before_end_value)
        , after_start(after_start_value)
        , after_end(after_end_value)
    {}

    /*!
    \brief Restores the pre-resize endpoints.
    \param context Apply-time editor/audio dependencies.
    \return Empty success, or the non-commit failure that should abort the transition.
    */
    [[nodiscard]] std::expected<void, EditorUndoFailureCode> undo(
        EditorEditContext& context) const override;

    /*!
    \brief Re-applies the post-resize endpoints.
    \param context Apply-time editor/audio dependencies.
    \return Empty success, or the non-commit failure that should abort the transition.
    */
    [[nodiscard]] std::expected<void, EditorUndoFailureCode> redo(
        EditorEditContext& context) const override;

    /*!
    \brief Returns the user-visible command label for menus and diagnostics.
    \return Human-readable label naming the resized region.
    */
    [[nodiscard]] std::string label() const override;

    /*! \brief Stable id of the resized region. */
    std::string region_id;

    /*! \brief User-facing region name used for the undo label. */
    std::string region_name;

    /*! \brief Musical start before the resize. */
    common::core::ToneGridPosition before_start;

    /*! \brief Musical end before the resize. */
    common::core::ToneGridPosition before_end;

    /*! \brief Musical start after the resize. */
    common::core::ToneGridPosition after_start;

    /*! \brief Musical end after the resize. */
    common::core::ToneGridPosition after_end;

private:
    [[nodiscard]] std::expected<void, EditorUndoFailureCode> applyEndpoints(
        EditorEditContext& context, common::core::ToneGridPosition start,
        common::core::ToneGridPosition end) const;
};

} // namespace rock_hero::editor::core
