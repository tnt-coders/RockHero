/*!
\file signal_chain_workflow.h
\brief Headless signal-chain editing state used by the editor controller.
*/

#pragma once

#include <cstddef>
#include <optional>
#include <rock_hero/common/audio/plugin_chain_snapshot.h>
#include <rock_hero/editor/core/plugin_view_state.h>
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

    /*! \brief Clears any pending browser insertion target. */
    void clearPendingInsertion() noexcept;

    /*!
    \brief Returns the insertion slot to use for the next browser selection.
    \return Current pending slot, append slot, or empty when the pending slot is stale.
    */
    [[nodiscard]] std::optional<std::size_t> insertionIndexForSelection() const noexcept;

    /*! \brief Reports whether the chain contains a plugin instance. */
    [[nodiscard]] bool containsInstance(std::string_view instance_id) const noexcept;

    /*!
    \brief Returns the current chain index for a plugin instance.
    \param instance_id Opaque plugin instance ID to find.
    \return Current chain index, or empty when the instance is stale.
    */
    [[nodiscard]] std::optional<std::size_t> chainIndexForInstance(
        std::string_view instance_id) const noexcept;

    /*! \brief Reports whether the current chain contains any loaded plugins. */
    [[nodiscard]] bool hasPlugins() const noexcept;

    /*! \brief Reports whether the chain can accept another user plugin. */
    [[nodiscard]] bool hasInsertCapacity() const noexcept;

    /*! \brief Returns the slot that appends after the current chain. */
    [[nodiscard]] std::size_t appendIndex() const noexcept;

    /*! \brief Returns view-ready plugin rows in authoritative backend order. */
    [[nodiscard]] const std::vector<PluginViewState>& plugins() const noexcept;

    /*!
    \brief Stores the editor-authored visual block placement reported by the view.

    The view owns the placement gesture math; the workflow holds the committed result so it persists
    on capture. The placement is aligned to the current chain order; a size mismatch is treated as a
    stale report and ignored.

    \param block_indices Fixed visual block for each plugin in current chain order.
    */
    void setBlockPlacement(const std::vector<std::size_t>& block_indices);

    /*!
    \brief Returns the authored visual block of each plugin in chain order for persistence.
    \return Fixed visual block per plugin, aligned to the current chain order.
    */
    [[nodiscard]] std::vector<std::size_t> blockIndices() const;

private:
    std::vector<PluginViewState> m_plugins;
    std::optional<std::size_t> m_pending_insertion_index;
};

} // namespace rock_hero::editor::core
