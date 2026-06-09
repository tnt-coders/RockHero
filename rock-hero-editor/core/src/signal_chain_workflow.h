/*!
\file signal_chain_workflow.h
\brief Headless signal-chain editing state used by the editor controller.
*/

#pragma once

#include <cstddef>
#include <optional>
#include <rock_hero/common/audio/plugin_chain_snapshot.h>
#include <rock_hero/editor/core/plugin_block_assignment.h>
#include <rock_hero/editor/core/plugin_view_state.h>
#include <string>
#include <string_view>
#include <vector>

namespace rock_hero::editor::core
{

/*!
\brief Owns editor-side signal-chain state without calling audio ports or UI code.

The controller applies authoritative snapshots returned by common/audio. The workflow validates
stale row IDs, remembers the pending browser insertion slot, and exposes view-ready plugin rows.
*/
class SignalChainWorkflow final
{
public:
    /*! \brief Creates empty signal-chain workflow state. */
    SignalChainWorkflow() = default;

    /*!
    \brief Replaces the current chain with an authoritative backend snapshot.
    \param snapshot Ordered plugin chain returned by common/audio.
    */
    void replaceSnapshot(common::audio::PluginChainSnapshot snapshot);

    /*! \brief Clears the current chain and any pending browser insertion target. */
    void clear();

    /*!
    \brief Starts a browser-backed insert at a specific chain slot.
    \param chain_index User-visible insertion slot while the current chain has capacity.
    \return True when the slot is current and has been stored.
    */
    [[nodiscard]] bool requestInsertAt(std::size_t chain_index);

    /*! \brief Starts a browser-backed append at the current chain end. */
    void requestAppend();

    /*!
    \brief Records the visual block a pending browser insert should occupy.

    Set after requestInsertAt for a specific-slot insert; the next insertion snapshot places the
    new plugin at this block while surviving plugins keep their authored blocks. Empty means no
    chosen block (an append), so the snapshot falls back to its default placement.

    \param block_index Fixed visual block for the pending insert, or empty for an append.
    */
    void setPendingInsertBlock(std::optional<std::size_t> block_index) noexcept;

    /*! \brief Clears any pending browser insertion target. */
    void clearPendingInsertion() noexcept;

    /*!
    \brief Returns the insertion slot to use for the next browser selection.
    \return Current pending slot, append slot, or empty when the pending slot is stale.
    */
    [[nodiscard]] std::optional<std::size_t> insertionIndexForSelection() const noexcept;

    /*!
    \brief Reports whether the chain contains a plugin instance.
    \param instance_id Opaque plugin instance ID to find.
    \return True when the current chain contains the instance.
    */
    [[nodiscard]] bool containsInstance(std::string_view instance_id) const noexcept;

    /*!
    \brief Returns the current chain index for a plugin instance.
    \param instance_id Opaque plugin instance ID to find.
    \return Current chain index, or empty when the instance is stale.
    */
    [[nodiscard]] std::optional<std::size_t> chainIndexForInstance(
        std::string_view instance_id) const noexcept;

    /*!
    \brief Reports whether the current chain contains any loaded plugins.
    \return True when the chain is non-empty.
    */
    [[nodiscard]] bool hasPlugins() const noexcept;

    /*!
    \brief Reports whether the chain can accept another user plugin.
    \return True when the current chain is below the user-plugin limit.
    */
    [[nodiscard]] bool hasInsertCapacity() const noexcept;

    /*!
    \brief Returns the slot that appends after the current chain.
    \return User-visible append insertion index.
    */
    [[nodiscard]] std::size_t appendIndex() const noexcept;

    /*!
    \brief Returns view-ready plugin rows in authoritative backend order.
    \return Current plugin rows.
    */
    [[nodiscard]] const std::vector<PluginViewState>& plugins() const noexcept;

    /*!
    \brief Stores the editor-authored visual block placement reported by the view.

    The view owns transient placement gesture math; the workflow holds the committed result so it
    persists on capture. Assignments are keyed by plugin instance ID so they survive backend
    reorders before being applied to the current chain.

    \param placement Fixed visual block assignments for current plugin instances.
    \return True when the normalized placement changed workflow state.
    */
    [[nodiscard]] bool setBlockPlacement(const std::vector<PluginBlockAssignment>& placement);

    /*!
    \brief Sets or clears the manual display type override for a plugin instance.
    \param instance_id Opaque plugin instance ID to update.
    \param display_type Manual display type, or empty to use automatic classification.
    \return True when the stored override changed.
    */
    [[nodiscard]] bool setPluginDisplayTypeOverride(
        std::string_view instance_id, std::optional<PluginDisplayType> display_type);

    /*!
    \brief Returns the authored visual block of each plugin in chain order for persistence.
    \return Fixed visual block per plugin, aligned to the current chain order.
    */
    [[nodiscard]] std::vector<std::size_t> blockIndices() const;

    /*!
    \brief Returns manual display type override tokens in chain order for persistence.
    \return Stable display override tokens per plugin, or empty entries for automatic display.
    */
    [[nodiscard]] std::vector<std::string> displayTypeOverrideTokens() const;

private:
    std::vector<PluginViewState> m_plugins;
    std::optional<std::size_t> m_pending_insertion_index;
    // Visual block a pending specific-slot insert should occupy; empty for an append.
    std::optional<std::size_t> m_pending_insert_block;
};

} // namespace rock_hero::editor::core
