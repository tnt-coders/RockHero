/*!
\file tone_branch_gain_plugin.h
\brief Private Tracktion plugin terminating one tone branch with an automatable gain.
*/

#pragma once

#include <atomic>
#include <rock_hero/common/audio/shared/gain.h>
#include <tracktion_engine/tracktion_engine.h>

namespace rock_hero::common::audio
{

/*!
\brief Hidden Tracktion plugin that scales one multi-tone rack branch by audibility and level.

This is the switch point of the multi-tone graph: every tone branch ends in one of these, the
region schedule is baked into this plugin's branch-gain automation curve, and the transport clock
evaluates the curve so tone switches never require a graph change. The branch gain is linear 0..1
branch audibility (1 = active tone, 0 = silent), written only by the schedule or the direct switch.

This is also the tone's own gain block, locked in the branch's final slot: the authored output level
lives here as a plain message-thread dB value that is never automated. Both feed one smoother
(target = branch gain x the level's linear gain), so a tone carries its level wherever the switch
happens — including a switch the audio thread makes from a baked schedule, which no message-thread
write could follow in time. `LiveRigGainPlugin` remains the message-thread dB trim for the rig's
input stage and for the post-rack monitor stage, neither of which belongs to any tone.
*/
class ToneBranchGainPlugin final : public tracktion::Plugin
{
public:
    /*! \brief Tracktion plugin type stored in the plugin ValueTree. */
    static const char* xmlTypeName;

    /*!
    \brief Creates the minimal Tracktion state tree for a tone branch gain plugin.
    \return Tracktion ValueTree containing the private plugin type.
    */
    [[nodiscard]] static juce::ValueTree createState();

    /*!
    \brief Creates a tone branch gain plugin from Tracktion plugin creation info.
    \param info Tracktion plugin creation context and state tree.
    */
    explicit ToneBranchGainPlugin(tracktion::PluginCreationInfo info);

    /*! \brief Detaches the parameter and notifies Tracktion listeners of destruction. */
    ~ToneBranchGainPlugin() override;

    /*! \brief Moving is disabled; Tracktion plugins are reference-counted graph nodes. */
    ToneBranchGainPlugin(ToneBranchGainPlugin&&) = delete;

    /*! \brief Move assignment is disabled; Tracktion plugins are reference-counted graph nodes. */
    ToneBranchGainPlugin& operator=(ToneBranchGainPlugin&&) = delete;

    /*!
    \brief Returns the user-visible plugin name for Tracktion diagnostics.
    \return Plugin name text.
    */
    [[nodiscard]] juce::String getName() const override;

    /*!
    \brief Returns the private Tracktion plugin type string.
    \return Plugin type identifier.
    */
    [[nodiscard]] juce::String getPluginType() override;

    /*!
    \brief Returns the owner shown by Tracktion diagnostics.
    \return Vendor text shown by Tracktion.
    */
    [[nodiscard]] juce::String getVendor() override;

    /*!
    \brief Returns the compact plugin name used by Tracktion lists.
    \param suggested_length Desired maximum display length from Tracktion.
    \return Short plugin name text.
    */
    [[nodiscard]] juce::String getShortName(int suggested_length) override;

    /*!
    \brief Returns the selectable description for Tracktion internals.
    \return Description text used by Tracktion selection models.
    */
    [[nodiscard]] juce::String getSelectableDescription() override;

    /*!
    \brief Allows the branch gain to terminate rack branches.
    \return Always true; this plugin exists to live inside the multi-tone rack.
    */
    [[nodiscard]] bool canBeAddedToRack() override;

    /*!
    \brief Prevents user/plugin-list movement of the private structural plugin.
    \return Always false.
    */
    [[nodiscard]] bool canBeMoved() override;

    /*!
    \brief Avoids CPU timing overhead for this tiny always-present utility.
    \return Always false.
    */
    [[nodiscard]] bool shouldMeasureCpuUsage() const noexcept override;

    /*!
    \brief Preserves the input channel count.
    \param num_input_channels Number of input channels Tracktion plans to provide.
    \return The same channel count so this plugin does not change routing width.
    */
    [[nodiscard]] int getNumOutputChannelsGivenInputs(int num_input_channels) override;

    /*!
    \brief Prepares smoothing from Tracktion playback settings.
    \param info Tracktion playback initialization context.
    */
    void initialise(const tracktion::PluginInitialisationInfo& info) override;

    /*!
    \brief Updates smoothing state when Tracktion reuses the plugin.
    \param info Tracktion playback initialization context.
    */
    void initialiseWithoutStopping(const tracktion::PluginInitialisationInfo& info) override;

    /*! \brief Releases playback resources. */
    void deinitialise() override;

    /*!
    \brief Applies the current automated branch gain to the render buffer.
    \param context Tracktion render block containing the audio buffer to scale.
    */
    void applyToBuffer(const tracktion::PluginRenderContext& context) override;

    /*!
    \brief Restores the branch gain from a Tracktion ValueTree.
    \param tree Tracktion state tree containing persisted gain properties.
    */
    void restorePluginStateFromValueTree(const juce::ValueTree& tree) override;

    /*!
    \brief Returns the automatable branch gain parameter for schedule baking and preview.
    \return Reference-counted pointer to the 0..1 branch gain parameter.
    */
    [[nodiscard]] tracktion::AutomatableParameter::Ptr branchGainParameter() const;

    /*!
    \brief Stores this tone's authored output level, multiplied into the branch gain.
    \param gain Desired level for the tone this branch plays; clamped to the accepted range.
    */
    void setOutputGain(Gain gain);

    /*!
    \brief Returns this tone's authored output level.
    \return Current clamped level for the tone this branch plays.
    */
    [[nodiscard]] Gain outputGain() const noexcept;

private:
    [[nodiscard]] float targetOutputLinearGain() const noexcept;
    void setTargetOutputGainDb(float gain_db) noexcept;
    void valueTreePropertyChanged(
        juce::ValueTree& changed_tree, const juce::Identifier& changed_property) override;

    juce::CachedValue<float> m_branch_gain;
    tracktion::AutomatableParameter::Ptr m_branch_gain_parameter;
    juce::CachedValue<float> m_output_gain_db;
    std::atomic<float> m_target_output_gain_db{static_cast<float>(defaultGainDb())};
    juce::SmoothedValue<float> m_smoothed_gain{1.0f};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ToneBranchGainPlugin)
};

} // namespace rock_hero::common::audio
