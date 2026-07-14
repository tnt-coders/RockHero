/*!
\file signal_chain_view.h
\brief JUCE view that renders the signal-chain state.
*/

#pragma once

#include "shared/audio_level_meter.h"
#include "signal_chain/signal_chain_block_layout.h"

#include <cstddef>
#include <cstdint>
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>
#include <optional>
#include <rock_hero/common/audio/input/audio_meter_snapshot.h>
#include <rock_hero/editor/core/signal_chain/plugin_block_assignment.h>
#include <rock_hero/editor/core/signal_chain/signal_chain_view_state.h>
#include <rock_hero/editor/core/tone_designer/tone_designer_view_state.h>
#include <string>
#include <vector>

namespace rock_hero::editor::ui
{

/*!
\brief Renders the current signal chain from framework-free state.

The view is intentionally linear for the first plugin-host UI. It renders project-owned state and
emits signal-chain intent through Listener so future rack or parallel-chain models can replace the
state shape without exposing Tracktion or JUCE plugin descriptions to the view.
*/
class SignalChainView final : public juce::Component, public juce::DragAndDropContainer
{
public:
    /*! \brief Listener for user intents emitted by the signal-chain view. */
    class Listener
    {
    public:
        /*! \brief Destroys the listener interface. */
        virtual ~Listener() = default;

        /*!
        \brief Called when the user requests the plugin browser for an insertion slot.
        \param chain_index User-visible insertion slot while the current chain has capacity.
        \param block_index Fixed visual block the inserted plugin should occupy.
        */
        virtual void onInsertPluginPressed(std::size_t chain_index, std::size_t block_index) = 0;

        /*!
        \brief Called when the user requests removal of a plugin instance.
        \param instance_id Opaque plugin instance ID selected by the user.
        */
        virtual void onRemovePluginPressed(std::string instance_id) = 0;

        /*!
        \brief Called when the user requests moving a plugin instance.
        \param instance_id Opaque plugin instance ID selected by the user.
        \param destination_index Final user-visible chain index for the instance.
        \param placement Fixed visual block assignments after the move.
        */
        virtual void onMovePluginPressed(
            std::string instance_id, std::size_t destination_index,
            std::vector<core::PluginBlockAssignment> placement) = 0;

        /*!
        \brief Called when the authored visual block placement changes and should be persisted.
        \param placement Fixed visual block assignments for current plugin instances.
        */
        virtual void onSignalChainPlacementChanged(
            std::vector<core::PluginBlockAssignment> placement) = 0;

        /*!
        \brief Called when the user sets or clears a plugin block display type override.
        \param instance_id Opaque plugin instance ID selected by the user.
        \param display_type Manual display type, or empty to use automatic classification.
        */
        virtual void onPluginDisplayTypeOverrideChanged(
            std::string instance_id, std::optional<core::PluginDisplayType> display_type) = 0;

        /*!
        \brief Called when the user requests a plugin instance editor window.
        \param instance_id Opaque plugin instance ID selected by the user.
        */
        virtual void onOpenPluginPressed(std::string instance_id) = 0;

        /*! \brief Called when the user requests input calibration. */
        virtual void onInputCalibrationPressed() = 0;

        /*!
        \brief Called when the user previews output gain during a slider drag.
        \param gain_db New output gain in decibels.
        */
        virtual void onOutputGainPreviewChanged(double gain_db) = 0;

        /*!
        \brief Called when the user commits the output gain slider value.
        \param gain_db New output gain in decibels.
        */
        virtual void onOutputGainChanged(double gain_db) = 0;

        /*! \brief Called when the user starts a fresh untitled Tone Designer document. */
        virtual void onNewTonePressed() = 0;

        /*! \brief Called when the user asks to open a tone file as the designer document. */
        virtual void onOpenToneFilePressed() = 0;

        /*! \brief Called when the user saves the designer document to its associated file. */
        virtual void onSaveTonePressed() = 0;

        /*! \brief Called when the user saves the designer document to a chosen tone file. */
        virtual void onSaveToneAsPressed() = 0;

        /*! \brief Called when the user asks to import a tone file into the active project tone. */
        virtual void onImportTonePressed() = 0;

        /*! \brief Called when the user asks to export the active project tone to a tone file. */
        virtual void onExportTonePressed() = 0;

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
    \brief Creates the view and stores the listener that receives user intent.
    \param listener Listener that receives signal-chain actions.
    */
    explicit SignalChainView(Listener& listener);

    /*! \brief Releases child controls. */
    ~SignalChainView() override;

    /*! \brief Copying is disabled because JUCE component ownership is not copyable. */
    SignalChainView(const SignalChainView&) = delete;

    /*! \brief Copy assignment is disabled because JUCE component ownership is not copyable. */
    SignalChainView& operator=(const SignalChainView&) = delete;

    /*! \brief Moving is disabled because child component registrations are not movable. */
    SignalChainView(SignalChainView&&) = delete;

    /*! \brief Move assignment is disabled because child registrations are not movable. */
    SignalChainView& operator=(SignalChainView&&) = delete;

    /*!
    \brief Applies the current signal-chain render state.
    \param state State derived by the editor controller.
    */
    void setState(const core::SignalChainViewState& state);

    /*!
    \brief Names the tone the panel is editing; the header shows "Signal Chain - <tone>".
    \param tone_name Selected tone's user-facing name, or empty for the bare title.
    */
    void setToneName(std::string tone_name);

    /*!
    \brief Applies the Tone Designer document state.

    While active, the header shows "Tone Designer - <document>" with a dirty marker and the
    New/Open/Save/Save As strip replaces the project tone title.

    \param state Designer document state derived by the editor controller.
    */
    void setToneDesignerState(const core::ToneDesignerViewState& state);

    /*!
    \brief Applies live-rig post-fader meter levels.
    \param input_level Level after the input gain fader.
    \param output_level Level after the output gain fader.
    */
    void setMeterLevels(
        common::audio::AudioMeterLevel input_level, common::audio::AudioMeterLevel output_level);

    /*!
    \brief Paints the signal-chain background, title, and plugin chain.
    \param g Graphics context used for drawing.
    */
    void paint(juce::Graphics& g) override;

    /*! \brief Lays out the signal-chain controls. */
    void resized() override;

private:
    class InsertSlotView;
    class PluginTileView;
    class SignalPathContent;

    enum class TileLayoutMotion : std::uint8_t
    {
        Immediate,
        Animated,
    };

    // Emits an insert intent for an empty fixed block location.
    void insertPluginAtBlockLocation(std::size_t block_index);

    // Emits a move intent for a plugin dropped on a fixed block location.
    void movePluginToBlockLocation(
        std::string instance_id, std::size_t source_index, std::size_t destination_index);

    // Completes a tile drop, falling back to the last valid preview when no target resolved.
    void completePluginDrop(
        const juce::var& drag_description,
        std::optional<SignalChainBlockLayout::DropIntent> intent);

    // Reports the current block placement so the controller can persist it with the project.
    void reportSignalChainPlacement();

    // Builds instance-keyed placement assignments from the current render cache.
    [[nodiscard]] std::vector<core::PluginBlockAssignment> pluginBlockAssignments() const;

    // Applies a transient drag preview and relayouts tiles into their preview positions.
    void previewPluginMove(std::size_t source_index, SignalChainBlockLayout::DropIntent intent);

    // Clears a transient drag preview after a drag cancels or completes.
    void clearPluginMovePreview();

    // Defers preview cleanup until pending JUCE drop callbacks have run.
    void clearPluginMovePreviewAsync();

    // Positions fixed block placeholders and plugin tiles inside the signal path.
    void layoutSignalPathContent(TileLayoutMotion motion);

    // Rebuilds tile components after controller-derived plugin state changes.
    void rebuildPluginTiles();

    // Listener that receives view user intents.
    Listener& m_listener;

    // Last render state pushed by the editor controller.
    core::SignalChainViewState m_state{};

    // Selected tone's name shown in the header title; empty paints the bare title.
    std::string m_tone_name{};

    // Tone Designer document state; while active it owns the header title and the file strip.
    core::ToneDesignerViewState m_tone_designer{};

    // Raw or calibrated input peak meter positioned on the left side of the plugin chain.
    AudioLevelMeter m_input_meter;

    // Button that opens the input calibration workflow.
    juce::TextButton m_input_calibrate_button;

    // Tone Designer file commands, visible only while the designer owns the live rig.
    juce::TextButton m_tone_new_button;
    juce::TextButton m_tone_open_button;
    juce::TextButton m_tone_save_button;
    juce::TextButton m_tone_save_as_button;

    // Project-mode tone-file commands (copy semantics), never visible alongside the designer
    // strip: the two button sets share the header's right side across the two modes.
    juce::TextButton m_tone_import_button;
    juce::TextButton m_tone_export_button;

    // Slider look-and-feel that keeps the default textbox while compacting the track.
    std::unique_ptr<juce::LookAndFeel> m_output_gain_slider_look_and_feel;

    // Output gain slider positioned within the right-side output gain group.
    juce::Slider m_output_gain_slider;

    // True while JUCE is issuing drag-scoped output gain value changes.
    bool m_output_gain_dragging{false};

    // Post-output-gain peak meter positioned beside the output slider.
    AudioLevelMeter m_output_meter;

    // Scrollable viewport that keeps long plugin chains reachable in a compact view.
    juce::Viewport m_chain_viewport;

    // Viewed component that paints the signal path and owns insert rails and plugin tiles.
    std::unique_ptr<SignalPathContent> m_chain_content;

    // Child placeholder controls for the fixed signal-chain block positions.
    std::vector<std::unique_ptr<InsertSlotView>> m_insert_slots;

    // Child tile controls for the current plugin chain.
    std::vector<std::unique_ptr<PluginTileView>> m_plugin_tiles;

    // Framework-free fixed-block placement and drag-preview state.
    SignalChainBlockLayout m_block_layout;
};

} // namespace rock_hero::editor::ui
