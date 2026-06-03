/*!
\file signal_chain_panel.h
\brief JUCE control panel for the plugin chain on the instrument route.
*/

#pragma once

#include "audio_level_meter.h"

#include <cstddef>
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>
#include <optional>
#include <rock_hero/common/audio/audio_meter_snapshot.h>
#include <rock_hero/editor/core/signal_chain_view_state.h>
#include <string>
#include <vector>

namespace rock_hero::editor::ui
{

/*!
\brief Displays the current plugin chain and emits panel intents.

The panel is intentionally linear for the first plugin-host UI. It renders project-owned state and
emits insert-plugin intent through Listener so future rack or parallel-chain models can replace the
state shape without exposing Tracktion or JUCE plugin descriptions to the view.
*/
class SignalChainPanel final : public juce::Component, public juce::DragAndDropContainer
{
public:
    /*! \brief Listener for user intents emitted by the signal-chain panel. */
    class Listener
    {
    public:
        /*! \brief Destroys the listener interface. */
        virtual ~Listener() = default;

        /*!
        \brief Called when the user requests the plugin browser for an insertion slot.
        \param chain_index User-visible insertion slot in [0, plugin_count].
        */
        virtual void onInsertPluginPressed(std::size_t chain_index) = 0;

        /*!
        \brief Called when the user requests removal of a plugin instance.
        \param instance_id Opaque plugin instance ID selected by the user.
        */
        virtual void onRemovePluginPressed(std::string instance_id) = 0;

        /*!
        \brief Called when the user requests moving a plugin instance.
        \param instance_id Opaque plugin instance ID selected by the user.
        \param destination_index Final user-visible chain index for the instance.
        */
        virtual void onMovePluginPressed(
            std::string instance_id, std::size_t destination_index) = 0;

        /*!
        \brief Called when the user requests a plugin instance editor window.
        \param instance_id Opaque plugin instance ID selected by the user.
        */
        virtual void onOpenPluginPressed(std::string instance_id) = 0;

        /*! \brief Called when the user requests input calibration. */
        virtual void onInputCalibrationPressed() = 0;

        /*!
        \brief Called when the user adjusts the output gain slider.
        \param gain_db New output gain in decibels.
        */
        virtual void onOutputGainChanged(double gain_db) = 0;

    protected:
        /*! \brief Creates the listener interface. */
        Listener() = default;

        /*! \brief Copies the listener interface. */
        Listener(const Listener&) = default;

        /*! \brief Moves the listener interface. */
        Listener(Listener&&) = default;

        /*!
        \brief Assigns the listener interface from another interface.
        \return Reference to this listener interface.
        */
        Listener& operator=(const Listener&) = default;

        /*!
        \brief Move-assigns the listener interface from another interface.
        \return Reference to this listener interface.
        */
        Listener& operator=(Listener&&) = default;
    };

    /*!
    \brief Creates the panel and stores the listener that receives user intent.
    \param listener Listener that receives panel actions.
    */
    explicit SignalChainPanel(Listener& listener);

    /*! \brief Releases child controls. */
    ~SignalChainPanel() override;

    /*! \brief Copying is disabled because JUCE component ownership is not copyable. */
    SignalChainPanel(const SignalChainPanel&) = delete;

    /*! \brief Copy assignment is disabled because JUCE component ownership is not copyable. */
    SignalChainPanel& operator=(const SignalChainPanel&) = delete;

    /*! \brief Moving is disabled because child component registrations are not movable. */
    SignalChainPanel(SignalChainPanel&&) = delete;

    /*! \brief Move assignment is disabled because child registrations are not movable. */
    SignalChainPanel& operator=(SignalChainPanel&&) = delete;

    /*!
    \brief Applies the current signal-chain render state.
    \param state State derived by the editor controller.
    */
    void setState(const core::SignalChainViewState& state);

    /*!
    \brief Applies live-rig post-fader meter levels.
    \param input_level Level after the input gain fader.
    \param output_level Level after the output gain fader.
    */
    void setMeterLevels(
        common::audio::AudioMeterLevel input_level, common::audio::AudioMeterLevel output_level);

    /*!
    \brief Paints the panel background, title, and plugin chain.
    \param g Graphics context used for drawing.
    */
    void paint(juce::Graphics& g) override;

    /*! \brief Lays out the panel controls. */
    void resized() override;

private:
    class InsertSlotView;
    class PluginTileView;
    class SignalPathContent;

    enum class TileLayoutMotion
    {
        Immediate,
        Animated,
    };

    // Transient layout state used while a drag hover previews a reordered chain.
    struct DragPreview
    {
        std::size_t source_index{};
        std::size_t destination_index{};
        std::vector<std::size_t> block_indices;
    };

    // Pending fixed-slot target for the next plugin inserted through this panel.
    struct PendingInsert
    {
        std::size_t chain_index{};
        std::size_t block_index{};
    };

    // Emits an insert intent for an empty fixed block location.
    void insertPluginAtBlockLocation(std::size_t block_index);

    // Emits a move intent for a plugin dropped on a fixed block location.
    void movePluginToBlockLocation(
        std::string instance_id, std::size_t source_index, std::size_t destination_index);

    // Applies a transient drag preview and relayouts tiles into their preview positions.
    void previewPluginMove(
        std::size_t source_index, std::size_t destination_index,
        std::vector<std::size_t> block_indices);

    // Keeps a successfully dropped preview visible until controller state catches up.
    void commitPluginMovePreview();

    // Applies a view-only block placement change that does not alter linear chain order.
    void applyPluginBlockIndices(std::vector<std::size_t> block_indices);

    // Clears a transient drag preview after a drop leaves the chain or completes.
    void clearPluginMovePreview();

    // Clears only previews that have not emitted a valid drop yet.
    void clearUncommittedPluginMovePreview();

    // Defers uncommitted preview cleanup until pending JUCE drop callbacks have run.
    void clearUncommittedPluginMovePreviewAsync();

    // Positions fixed block placeholders and plugin tiles inside the signal path.
    void layoutSignalPathContent(TileLayoutMotion motion);

    // Rebuilds tile components after controller-derived plugin state changes.
    void rebuildPluginTiles();

    // Listener that receives panel user intents.
    Listener& m_listener;

    // Last render state pushed by the editor controller.
    core::SignalChainViewState m_state{};

    // Raw or calibrated input peak meter positioned on the left side of the plugin chain.
    AudioLevelMeter m_input_meter;

    // Button that opens the input calibration workflow.
    juce::TextButton m_input_calibrate_button;

    // Slider look-and-feel that keeps the default textbox while compacting the track.
    std::unique_ptr<juce::LookAndFeel> m_output_gain_slider_look_and_feel;

    // Output gain slider positioned within the right-side output gain group.
    juce::Slider m_output_gain_slider;

    // Post-output-gain peak meter positioned beside the output slider.
    AudioLevelMeter m_output_meter;

    // Scrollable viewport that keeps long plugin chains reachable in a compact panel.
    juce::Viewport m_chain_viewport;

    // Viewed component that paints the signal path and owns insert rails and plugin tiles.
    std::unique_ptr<SignalPathContent> m_chain_content;

    // Child placeholder controls for the fixed signal-chain block positions.
    std::vector<std::unique_ptr<InsertSlotView>> m_insert_slots;

    // Child tile controls for the current plugin chain.
    std::vector<std::unique_ptr<PluginTileView>> m_plugin_tiles;

    // Stable visual block position for each plugin in the controller-provided linear chain.
    std::vector<std::size_t> m_plugin_block_indices;

    // Fixed block selected for a plugin browser insertion that has not reached state yet.
    std::optional<PendingInsert> m_pending_insert;

    // Active drag-hover preview, when a valid reorder target is under the pointer.
    std::optional<DragPreview> m_drag_preview;

    // True once a drop emitted a move intent and is waiting for authoritative controller state.
    bool m_drag_preview_committed{false};
};

} // namespace rock_hero::editor::ui
