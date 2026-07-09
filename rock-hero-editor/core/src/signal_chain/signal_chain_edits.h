/*!
\file signal_chain_edits.h
\brief Concrete signal-chain undo edit objects applied through the editor undo history.
*/

#pragma once

#include "controller/editor_undo_history.h"

#include <cstddef>
#include <expected>
#include <optional>
#include <rock_hero/common/audio/plugin/i_plugin_host.h>
#include <rock_hero/common/audio/shared/gain.h>
#include <rock_hero/common/core/tone/tone_automation.h>
#include <rock_hero/editor/core/signal_chain/plugin_block_assignment.h>
#include <rock_hero/editor/core/signal_chain/plugin_display_type.h>
#include <string>
#include <vector>

namespace rock_hero::editor::core
{

/*! \brief Editor-owned visual state for one plugin instance across recreate. */
struct [[nodiscard]] PluginVisualEditState
{
    /*! \brief Plugin instance ID the visual state belongs to. */
    std::string instance_id;

    /*! \brief Fixed visual block occupied by the plugin. */
    std::size_t block_index{};

    /*! \brief Manual display type override before removal or after insertion. */
    std::optional<PluginDisplayType> display_type_override;

    /*! \brief Compares two visual edit states by their stored values. */
    friend bool operator==(const PluginVisualEditState& lhs, const PluginVisualEditState& rhs) =
        default;
};

/*! \brief Edit that removes or recreates a newly inserted plugin. */
struct [[nodiscard]] PluginInsertEdit final : IEdit
{
    /*! \brief Inserted plugin instance ID. */
    std::string instance_id;

    /*! \brief Plugin display name captured at construction, for the undo label. */
    std::string plugin_name;

    /*! \brief Name of the tone whose chain received the plugin, for the undo label. */
    std::string tone_name;

    /*! \brief Chain index where the plugin was inserted. */
    std::size_t chain_index{};

    /*! \brief Full plugin state captured after insertion for id-preserving redo. */
    common::audio::PluginInstanceState plugin_state;

    /*! \brief Placement before insertion. */
    std::vector<PluginBlockAssignment> before_placement;

    /*! \brief Placement after insertion. */
    std::vector<PluginBlockAssignment> after_placement;

    /*! \brief Visual state after insertion. */
    PluginVisualEditState visual_state;

    [[nodiscard]] std::expected<void, EditorUndoFailureCode> undo(
        EditorEditContext& context) const override;
    [[nodiscard]] std::expected<void, EditorUndoFailureCode> redo(
        EditorEditContext& context) const override;
    [[nodiscard]] std::string label() const override;
    [[nodiscard]] bool instantiatesPlugin(EditorUndoDirection direction) const override;
};

/*! \brief Edit that recreates or removes a deleted plugin. */
struct [[nodiscard]] PluginRemoveEdit final : IEdit
{
    /*! \brief Removed plugin instance ID. */
    std::string instance_id;

    /*! \brief Plugin display name captured at construction, for the undo label. */
    std::string plugin_name;

    /*! \brief Name of the tone whose chain the plugin was removed from, for the undo label. */
    std::string tone_name;

    /*! \brief Chain index occupied before removal. */
    std::size_t chain_index{};

    /*! \brief Full plugin state captured before removal for id-preserving undo. */
    common::audio::PluginInstanceState plugin_state;

    /*! \brief Placement before removal. */
    std::vector<PluginBlockAssignment> before_placement;

    /*! \brief Placement after removal. */
    std::vector<PluginBlockAssignment> after_placement;

    /*! \brief Visual state before removal. */
    PluginVisualEditState visual_state;

    /*! \brief Durable automation id of the removed plugin; keys its automation entries. */
    std::string plugin_id;

    /*! \brief Tone whose chain owned the plugin, used to rebuild derived curves on undo. */
    std::string tone_document_ref;

    /*!
    \brief Tone-parameter automation removed with the plugin, restored verbatim on undo.

    Automation is keyed by durable plugin id, so deleting the plugin would otherwise strand these
    entries as unresolvable lanes. They travel out with the plugin and back in on undo.
    */
    std::vector<common::core::ToneParameterAutomation> removed_automation;

    [[nodiscard]] std::expected<void, EditorUndoFailureCode> undo(
        EditorEditContext& context) const override;
    [[nodiscard]] std::expected<void, EditorUndoFailureCode> redo(
        EditorEditContext& context) const override;
    [[nodiscard]] std::string label() const override;
    [[nodiscard]] bool instantiatesPlugin(EditorUndoDirection direction) const override;
};

/*! \brief Edit that moves one plugin and restores its visual placement. */
struct [[nodiscard]] PluginMoveEdit final : IEdit
{
    /*! \brief Moved plugin instance ID. */
    std::string instance_id;

    /*! \brief Plugin display name captured at construction, for the undo label. */
    std::string plugin_name;

    /*! \brief Name of the tone whose chain the plugin moved within, for the undo label. */
    std::string tone_name;

    /*! \brief Chain index before the move. */
    std::size_t before_index{};

    /*! \brief Chain index after the move. */
    std::size_t after_index{};

    /*! \brief Placement before the move. */
    std::vector<PluginBlockAssignment> before_placement;

    /*! \brief Placement after the move. */
    std::vector<PluginBlockAssignment> after_placement;

    [[nodiscard]] std::expected<void, EditorUndoFailureCode> undo(
        EditorEditContext& context) const override;
    [[nodiscard]] std::expected<void, EditorUndoFailureCode> redo(
        EditorEditContext& context) const override;
    [[nodiscard]] std::string label() const override;
};

/*! \brief Edit that restores signal-chain block placement without touching audio. */
struct [[nodiscard]] PluginPlacementEdit final : IEdit
{
    /*! \brief Name of the tone whose block layout changed, for the undo label. */
    std::string tone_name;

    /*! \brief Placement before the edit. */
    std::vector<PluginBlockAssignment> before_placement;

    /*! \brief Placement after the edit. */
    std::vector<PluginBlockAssignment> after_placement;

    [[nodiscard]] std::expected<void, EditorUndoFailureCode> undo(
        EditorEditContext& context) const override;
    [[nodiscard]] std::expected<void, EditorUndoFailureCode> redo(
        EditorEditContext& context) const override;
    [[nodiscard]] std::string label() const override;
};

/*! \brief Edit that restores one plugin's manual display type override. */
struct [[nodiscard]] PluginDisplayTypeEdit final : IEdit
{
    /*! \brief Edited plugin instance ID. */
    std::string instance_id;

    /*! \brief Plugin display name captured at construction, for the undo label. */
    std::string plugin_name;

    /*! \brief Name of the tone owning the plugin, for the undo label. */
    std::string tone_name;

    /*! \brief Display type override before the edit. */
    std::optional<PluginDisplayType> before_type;

    /*! \brief Display type override after the edit. */
    std::optional<PluginDisplayType> after_type;

    [[nodiscard]] std::expected<void, EditorUndoFailureCode> undo(
        EditorEditContext& context) const override;
    [[nodiscard]] std::expected<void, EditorUndoFailureCode> redo(
        EditorEditContext& context) const override;
    [[nodiscard]] std::string label() const override;
};

/*! \brief Edit that restores one plugin's full opaque state. */
struct [[nodiscard]] PluginStateEdit final : IEdit
{
    /*! \brief Edited plugin instance ID. */
    std::string instance_id;

    /*! \brief Name of the tone owning the plugin, for the undo label. */
    std::string tone_name;

    /*! \brief Chain position captured for the undo label; empty when the instance did not resolve. */
    std::optional<std::size_t> chain_index;

    /*! \brief Full plugin state before the edit settled. */
    common::audio::PluginInstanceState before_state;

    /*! \brief Full plugin state after the edit settled. */
    common::audio::PluginInstanceState after_state;

    /*! \brief Display-only label hint for the plugin or state change. */
    std::string label_hint;

    [[nodiscard]] std::expected<void, EditorUndoFailureCode> undo(
        EditorEditContext& context) const override;
    [[nodiscard]] std::expected<void, EditorUndoFailureCode> redo(
        EditorEditContext& context) const override;
    [[nodiscard]] std::string label() const override;
};

/*! \brief Edit that restores the fixed output-gain plugin value. */
struct [[nodiscard]] OutputGainEdit final : IEdit
{
    /*! \brief Output gain before the edit. */
    common::audio::Gain before_gain;

    /*! \brief Output gain after the edit. */
    common::audio::Gain after_gain;

    [[nodiscard]] std::expected<void, EditorUndoFailureCode> undo(
        EditorEditContext& context) const override;
    [[nodiscard]] std::expected<void, EditorUndoFailureCode> redo(
        EditorEditContext& context) const override;
    [[nodiscard]] std::string label() const override;
};

} // namespace rock_hero::editor::core
