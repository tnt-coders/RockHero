/*!
\file tone_region_edits.h
\brief Concrete tone-track undo edit objects applied through the editor undo history.
*/

#pragma once

#include "controller/editor_undo_history.h"

#include <cstddef>
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

/*! \brief Inverse-command edit that removes a created tone-change region, and re-splits on redo. */
struct [[nodiscard]] ToneRegionCreateEdit final : IEdit
{
    /*!
    \brief Captures the split introduced by a tone-region create.
    \param at_value Grid position at which the region was split.
    \param new_region_id_value Id minted for the region beginning at \p at_value.
    \param tone_document_ref_value Catalog tone the new region references.
    \param tone_name_value User-facing tone name used for the undo label.
    */
    ToneRegionCreateEdit(
        common::core::ToneGridPosition at_value, std::string new_region_id_value,
        std::string tone_document_ref_value, std::string tone_name_value)
        : at(at_value)
        , new_region_id(std::move(new_region_id_value))
        , tone_document_ref(std::move(tone_document_ref_value))
        , tone_name(std::move(tone_name_value))
    {}

    /*!
    \brief Removes the created region, merging its span back into the region it was split from.
    \param context Apply-time editor/audio dependencies.
    \return Empty success, or the non-commit failure that should abort the transition.
    */
    [[nodiscard]] std::expected<void, EditorUndoFailureCode> undo(
        EditorEditContext& context) const override;

    /*!
    \brief Re-splits the region at the marker, recreating the tone-change region.
    \param context Apply-time editor/audio dependencies.
    \return Empty success, or the non-commit failure that should abort the transition.
    */
    [[nodiscard]] std::expected<void, EditorUndoFailureCode> redo(
        EditorEditContext& context) const override;

    /*! \brief Returns the user-visible command label for menus and diagnostics.
    \return Human-readable label naming the inserted tone. */
    [[nodiscard]] std::string label() const override;

    /*! \brief Grid position at which the region was split. */
    common::core::ToneGridPosition at;

    /*! \brief Id minted for the region beginning at the marker. */
    std::string new_region_id;

    /*! \brief Catalog tone the new region references. */
    std::string tone_document_ref;

    /*! \brief User-facing tone name used for the undo label. */
    std::string tone_name;
};

/*! \brief Inverse-command edit that restores a deleted tone region, and re-deletes on redo. */
struct [[nodiscard]] ToneRegionDeleteEdit final : IEdit
{
    /*!
    \brief Captures the region removed by a tone-region delete and how coverage was preserved.
    \param removed_index_value Index the region occupied before removal.
    \param removed_region_value Full region that was removed.
    \param region_name_value User-facing tone name used for the undo label.
    \param absorbed_by_prev_value True when the previous region absorbed the removed span; false
    when the removed region was first and the next region's start was extended back instead.
    */
    ToneRegionDeleteEdit(
        std::size_t removed_index_value, common::core::ToneRegion removed_region_value,
        std::string region_name_value, bool absorbed_by_prev_value)
        : removed_index(removed_index_value)
        , removed_region(std::move(removed_region_value))
        , region_name(std::move(region_name_value))
        , absorbed_by_prev(absorbed_by_prev_value)
    {}

    /*!
    \brief Reinserts the removed region and shrinks the neighbor that had absorbed its span.
    \param context Apply-time editor/audio dependencies.
    \return Empty success, or the non-commit failure that should abort the transition.
    */
    [[nodiscard]] std::expected<void, EditorUndoFailureCode> undo(
        EditorEditContext& context) const override;

    /*!
    \brief Re-deletes the region, merging its span back into the same neighbor.
    \param context Apply-time editor/audio dependencies.
    \return Empty success, or the non-commit failure that should abort the transition.
    */
    [[nodiscard]] std::expected<void, EditorUndoFailureCode> redo(
        EditorEditContext& context) const override;

    /*! \brief Returns the user-visible command label for menus and diagnostics.
    \return Human-readable label naming the removed region's tone. */
    [[nodiscard]] std::string label() const override;

    /*! \brief Index the region occupied before removal. */
    std::size_t removed_index;

    /*! \brief Full region that was removed. */
    common::core::ToneRegion removed_region;

    /*! \brief User-facing tone name used for the undo label. */
    std::string region_name;

    /*! \brief True when the previous region absorbed the removed span rather than the next. */
    bool absorbed_by_prev;
};

/*! \brief Inverse-command edit that restores a tone's previous catalog name. */
struct [[nodiscard]] ToneRenameEdit final : IEdit
{
    /*!
    \brief Captures a tone-catalog rename.
    \param tone_document_ref_value Document ref of the renamed catalog tone.
    \param before_name_value Catalog name before the rename.
    \param after_name_value Catalog name after the rename.
    */
    ToneRenameEdit(
        std::string tone_document_ref_value, std::string before_name_value,
        std::string after_name_value)
        : tone_document_ref(std::move(tone_document_ref_value))
        , before_name(std::move(before_name_value))
        , after_name(std::move(after_name_value))
    {}

    /*!
    \brief Restores the pre-rename catalog name.
    \param context Apply-time editor/audio dependencies.
    \return Empty success, or the non-commit failure that should abort the transition.
    */
    [[nodiscard]] std::expected<void, EditorUndoFailureCode> undo(
        EditorEditContext& context) const override;

    /*!
    \brief Re-applies the post-rename catalog name.
    \param context Apply-time editor/audio dependencies.
    \return Empty success, or the non-commit failure that should abort the transition.
    */
    [[nodiscard]] std::expected<void, EditorUndoFailureCode> redo(
        EditorEditContext& context) const override;

    /*! \brief Returns the user-visible command label for menus and diagnostics.
    \return Human-readable label naming the renamed tone. */
    [[nodiscard]] std::string label() const override;

    /*! \brief Document ref of the renamed catalog tone. */
    std::string tone_document_ref;

    /*! \brief Catalog name before the rename. */
    std::string before_name;

    /*! \brief Catalog name after the rename. */
    std::string after_name;

private:
    [[nodiscard]] std::expected<void, EditorUndoFailureCode> applyName(
        EditorEditContext& context, const std::string& name) const;
};

} // namespace rock_hero::editor::core
