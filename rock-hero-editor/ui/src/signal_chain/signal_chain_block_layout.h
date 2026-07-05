/*!
\file signal_chain_block_layout.h
\brief Framework-free fixed-block interaction state for SignalChainView.
*/

#pragma once

#include <cstddef>
#include <optional>
#include <rock_hero/editor/core/signal_chain/signal_chain_block_placement.h>
#include <rock_hero/editor/core/signal_chain/signal_chain_view_state.h>
#include <vector>

namespace rock_hero::editor::ui
{

/*!
\brief Coordinates fixed-block placement and drag-preview state for the signal-chain view.

SignalChainBlockLayout is private UI-module logic, but it deliberately avoids JUCE. The view feeds
it controller-derived plugin snapshots and drag/drop events; the layout caches the controller
placement for rendering, computes transient drag previews, and reports which move intent, if any,
the view must emit.
*/
class SignalChainBlockLayout final
{
public:
    /*! \brief Final linear destination plus visual placement produced by a block drop. */
    struct [[nodiscard]] DropIntent
    {
        /*! \brief Destination index to request from the controller. */
        std::size_t destination_index{};

        /*! \brief Visual placement the drop would produce. */
        core::SignalChainBlockPlacement placement;
    };

    /*! \brief Result of completing a drag/drop operation against the current preview state. */
    struct [[nodiscard]] DropCompletion
    {
        /*! \brief True when the view must relayout because the visual placement changed. */
        bool layout_changed{false};

        /*! \brief Destination index to send to the controller when the drop reorders plugins. */
        std::optional<std::size_t> move_destination_index{};
    };

    /*!
    \brief Creates a block-layout state model with a fixed minimum visual capacity.
    \param minimum_block_count Minimum number of fixed block positions to expose.
    */
    explicit SignalChainBlockLayout(std::size_t minimum_block_count);

    /*!
    \brief Applies a controller-provided plugin snapshot and cached visual placement.
    \param plugins Current linear plugin state from the controller.
    */
    void applyPlugins(const std::vector<core::PluginViewState>& plugins);

    /*!
    \brief Maps an empty fixed block to the matching insertion index.
    \param block_index Empty fixed block selected by the user.
    \return Linear insertion index to send to the controller, or empty when invalid.
    */
    [[nodiscard]] std::optional<std::size_t> insertionIndexForBlock(std::size_t block_index) const;

    /*!
    \brief Resolves a fixed-block drop into a visual placement and controller destination.
    \param source_index Plugin being dragged.
    \param target_block_index Fixed visual block receiving the drop.
    \return Drop intent, or empty when source or target is outside the current block layout.
    */
    [[nodiscard]] std::optional<DropIntent> dropIntent(
        std::size_t source_index, std::size_t target_block_index) const;

    /*!
    \brief Reports whether a fixed block can receive the dragged plugin.
    \param source_index Plugin being dragged.
    \param target_block_index Fixed visual block to test.
    \return True when the block has a valid empty or occupied drop result.
    */
    [[nodiscard]] bool canReceiveDrop(
        std::size_t source_index, std::size_t target_block_index) const;

    /*!
    \brief Applies a transient drag preview.
    \param source_index Plugin being dragged.
    \param intent Destination and placement represented by the preview.
    \return True when the active visual placement changed.
    */
    [[nodiscard]] bool previewMove(std::size_t source_index, DropIntent intent);

    /*!
    \brief Completes a drop, falling back to the last valid preview when the intent is missing.
    \param source_index Plugin being dropped.
    \param intent Drop intent resolved by the current target, if one is valid.
    \return Relayout and controller-move information for the view to apply.
    */
    [[nodiscard]] DropCompletion completeDrop(
        std::size_t source_index, std::optional<DropIntent> intent);

    /*!
    \brief Clears any active drag preview.
    \return True when clearing the preview changed the active placement.
    */
    [[nodiscard]] bool clearPreview();

    /*!
    \brief Returns the cached controller placement for the current plugin order.
    \return Placement last committed from controller state.
    */
    [[nodiscard]] const core::SignalChainBlockPlacement& cachedPlacement() const noexcept;

    /*!
    \brief Returns the placement the view should currently render.
    \return Drag-preview placement when active, otherwise the cached controller placement.
    */
    [[nodiscard]] const core::SignalChainBlockPlacement& activePlacement() const noexcept;

    /*!
    \brief Returns the number of visual blocks for the current plugin state.
    \return Fixed visual block count.
    */
    [[nodiscard]] std::size_t blockCount() const noexcept;

    /*!
    \brief Returns the fixed block currently assigned to one plugin.
    \param plugin_index Plugin index in controller order.
    \return Fixed block index, or empty when the plugin index is invalid.
    */
    [[nodiscard]] std::optional<std::size_t> blockForPlugin(
        std::size_t plugin_index) const noexcept;

private:
    // Last valid drag-hover preview, kept active while the pointer crosses invalid targets.
    struct DragPreview
    {
        std::size_t source_index{};
        std::size_t destination_index{};
        core::SignalChainBlockPlacement placement;
    };

    // Number of visual blocks for a plugin count, honoring the configured minimum.
    [[nodiscard]] std::size_t blockCountFor(std::size_t plugin_count) const noexcept;

    // Keeps a fixed-block placement that leaves linear plugin order unchanged; returns whether the
    // active visual placement changed.
    [[nodiscard]] bool applyPlacement(core::SignalChainBlockPlacement placement);

    // Applies the current preview when release lacks a fresh target intent.
    [[nodiscard]] std::optional<DropCompletion> completeCurrentPreviewDrop(
        std::size_t source_index);

    std::size_t m_minimum_block_count{};
    std::vector<core::PluginViewState> m_plugins;
    core::SignalChainBlockPlacement m_placement;
    std::optional<DragPreview> m_drag_preview;
};

} // namespace rock_hero::editor::ui
