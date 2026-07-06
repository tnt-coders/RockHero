/*!
\file tone_branch_gain_plugin.h
\brief Private Tracktion plugin terminating one tone branch with an automatable gain.
*/

#pragma once

#include <tracktion_engine/tracktion_engine.h>

namespace rock_hero::common::audio
{

/*!
\brief Hidden Tracktion plugin that scales one multi-tone rack branch by an automatable gain.

This is the switch point of the multi-tone graph: every tone branch ends in one of these, the
region schedule is baked into this plugin's gain automation curve, and the transport clock
evaluates the curve so tone switches never require a graph change. The gain is linear 0..1 branch
audibility (1 = active tone, 0 = silent), not a user-facing dB trim; `LiveRigGainPlugin` remains
the message-thread dB trim for rig input/output stages.
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

private:
    juce::CachedValue<float> m_branch_gain;
    tracktion::AutomatableParameter::Ptr m_branch_gain_parameter;
    juce::SmoothedValue<float> m_smoothed_gain{1.0f};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ToneBranchGainPlugin)
};

} // namespace rock_hero::common::audio
