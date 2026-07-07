/*!
\file tone_region_edits.h
\brief Concrete tone-track undo edit objects applied through the editor undo history.
*/

#pragma once

#include "controller/editor_undo_history.h"

#include <cstddef>
#include <expected>
#include <optional>
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
    \param position_value Grid position at which the region was split.
    \param new_region_id_value Id minted for the region beginning at \p position_value.
    \param tone_document_ref_value Catalog tone the new region references.
    \param tone_name_value User-facing tone name used for the undo label.
    */
    ToneRegionCreateEdit(
        common::core::ToneGridPosition position_value, std::string new_region_id_value,
        std::string tone_document_ref_value, std::string tone_name_value)
        : position(position_value)
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
    common::core::ToneGridPosition position;

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
    \param removed_catalog_tone_value Catalog tone the delete pruned because this region was its
    last reference, or empty when other regions still reference the tone.
    */
    ToneRegionDeleteEdit(
        std::size_t removed_index_value, common::core::ToneRegion removed_region_value,
        std::string region_name_value, bool absorbed_by_prev_value,
        std::optional<common::core::Tone> removed_catalog_tone_value)
        : removed_index(removed_index_value)
        , removed_region(std::move(removed_region_value))
        , region_name(std::move(region_name_value))
        , absorbed_by_prev(absorbed_by_prev_value)
        , removed_catalog_tone(std::move(removed_catalog_tone_value))
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

    /*! \brief Catalog tone pruned with the region's last reference; restored on undo. */
    std::optional<common::core::Tone> removed_catalog_tone;
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

/*! \brief Inverse-command edit that restores the previous position of a shared region boundary. */
struct [[nodiscard]] ToneBoundaryMoveEdit final : IEdit
{
    /*!
    \brief Captures a shared-boundary move between two adjacent regions.
    \param right_region_id_value Region on the later side of the boundary.
    \param before_position_value Boundary position before the move.
    \param after_position_value Boundary position after the move.
    */
    ToneBoundaryMoveEdit(
        std::string right_region_id_value, common::core::ToneGridPosition before_position_value,
        common::core::ToneGridPosition after_position_value)
        : right_region_id(std::move(right_region_id_value))
        , before_position(before_position_value)
        , after_position(after_position_value)
    {}

    /*!
    \brief Restores the pre-move boundary position across both neighbors.
    \param context Apply-time editor/audio dependencies.
    \return Empty success, or the non-commit failure that should abort the transition.
    */
    [[nodiscard]] std::expected<void, EditorUndoFailureCode> undo(
        EditorEditContext& context) const override;

    /*!
    \brief Re-applies the post-move boundary position across both neighbors.
    \param context Apply-time editor/audio dependencies.
    \return Empty success, or the non-commit failure that should abort the transition.
    */
    [[nodiscard]] std::expected<void, EditorUndoFailureCode> redo(
        EditorEditContext& context) const override;

    /*! \brief Returns the user-visible command label for menus and diagnostics.
    \return Human-readable label for the boundary move. */
    [[nodiscard]] std::string label() const override;

    /*! \brief Region on the later side of the boundary. */
    std::string right_region_id;

    /*! \brief Boundary position before the move. */
    common::core::ToneGridPosition before_position;

    /*! \brief Boundary position after the move. */
    common::core::ToneGridPosition after_position;

private:
    [[nodiscard]] std::expected<void, EditorUndoFailureCode> applyBoundary(
        EditorEditContext& context, common::core::ToneGridPosition position) const;
};

/*!
\brief Inverse-command edit for creating a new tone: removes the catalog tone and its region on
undo, and recreates both on redo.

Purely a model edit. The minted tone document file and the rig branch it loads into are left in
place across undo/redo; the branch is unselected and harmless, and the model stays the source of
truth for the next full rig reload.
*/
struct [[nodiscard]] ToneCreateWithNewToneEdit final : IEdit
{
    /*!
    \brief Captures a new-tone creation (a catalog entry plus the split region referencing it).
    \param position_value Grid position at which the region was split.
    \param new_region_id_value Id minted for the region beginning at \p position_value.
    \param tone_document_ref_value Freshly minted tone the new region and catalog entry reference.
    \param name_value User-facing name given to the new tone.
    */
    ToneCreateWithNewToneEdit(
        common::core::ToneGridPosition position_value, std::string new_region_id_value,
        std::string tone_document_ref_value, std::string name_value)
        : position(position_value)
        , new_region_id(std::move(new_region_id_value))
        , tone_document_ref(std::move(tone_document_ref_value))
        , name(std::move(name_value))
    {}

    /*!
    \brief Removes the created region and its catalog tone.
    \param context Apply-time editor/audio dependencies.
    \return Empty success, or the non-commit failure that should abort the transition.
    */
    [[nodiscard]] std::expected<void, EditorUndoFailureCode> undo(
        EditorEditContext& context) const override;

    /*!
    \brief Recreates the split region and re-adds its catalog tone.
    \param context Apply-time editor/audio dependencies.
    \return Empty success, or the non-commit failure that should abort the transition.
    */
    [[nodiscard]] std::expected<void, EditorUndoFailureCode> redo(
        EditorEditContext& context) const override;

    /*! \brief Returns the user-visible command label for menus and diagnostics.
    \return Human-readable label naming the new tone. */
    [[nodiscard]] std::string label() const override;

    /*! \brief Grid position at which the region was split. */
    common::core::ToneGridPosition position;

    /*! \brief Id minted for the region beginning at the marker. */
    std::string new_region_id;

    /*! \brief Freshly minted tone the new region and catalog entry reference. */
    std::string tone_document_ref;

    /*! \brief User-facing name given to the new tone. */
    std::string name;
};

/*!
\brief Inverse-command edit for resetting the sole tone region: repoints it (and its catalog entry)
between the previous tone and a fresh empty "Default" tone.

Purely a model edit. The freshly minted document file and both rig branches are left in place across
undo/redo; the model stays the source of truth for the next full rig reload.
*/
struct [[nodiscard]] ToneResetEdit final : IEdit
{
    /*!
    \brief Captures the reset of the sole region to a fresh empty tone.
    \param region_id_value Id of the sole region being reset.
    \param before_ref_value Tone the region referenced before the reset.
    \param before_name_value Catalog name of that tone before the reset.
    \param after_ref_value Freshly minted empty tone the region references after the reset.
    */
    ToneResetEdit(
        std::string region_id_value, std::string before_ref_value, std::string before_name_value,
        std::string after_ref_value)
        : region_id(std::move(region_id_value))
        , before_ref(std::move(before_ref_value))
        , before_name(std::move(before_name_value))
        , after_ref(std::move(after_ref_value))
    {}

    /*!
    \brief Repoints the region and its catalog entry back to the previous tone.
    \param context Apply-time editor/audio dependencies.
    \return Empty success, or the non-commit failure that should abort the transition.
    */
    [[nodiscard]] std::expected<void, EditorUndoFailureCode> undo(
        EditorEditContext& context) const override;

    /*!
    \brief Repoints the region and its catalog entry to the fresh empty "Default" tone.
    \param context Apply-time editor/audio dependencies.
    \return Empty success, or the non-commit failure that should abort the transition.
    */
    [[nodiscard]] std::expected<void, EditorUndoFailureCode> redo(
        EditorEditContext& context) const override;

    /*! \brief Returns the user-visible command label for menus and diagnostics.
    \return Human-readable label for the reset. */
    [[nodiscard]] std::string label() const override;

    /*! \brief Id of the sole region being reset. */
    std::string region_id;

    /*! \brief Tone the region referenced before the reset. */
    std::string before_ref;

    /*! \brief Catalog name of that tone before the reset. */
    std::string before_name;

    /*! \brief Freshly minted empty tone the region references after the reset. */
    std::string after_ref;

private:
    [[nodiscard]] std::expected<void, EditorUndoFailureCode> applyReset(
        EditorEditContext& context, const std::string& region_ref, const std::string& catalog_from,
        const std::string& catalog_to, const std::string& catalog_name) const;
};

} // namespace rock_hero::editor::core
