/*!
\file live_rig_gain_plugin.h
\brief Private Tracktion plugin used for live-rig input and output gain stages.
*/

#pragma once

#include <atomic>
#include <rock_hero/common/audio/gain.h>
#include <tracktion_engine/tracktion_engine.h>

namespace rock_hero::common::audio
{

/*!
\brief Hidden Tracktion plugin that applies project-owned live-rig gain.

This is an adapter-private structural plugin, not a user-browsable effect. It keeps the public gain
policy in common::audio::Gain while giving the Tracktion graph a normal plugin node to process.
*/
class LiveRigGainPlugin final : public tracktion::Plugin
{
public:
    /*! \brief Tracktion plugin type stored in the plugin ValueTree. */
    static const char* xmlTypeName;

    /*!
    \brief Creates the minimal Tracktion state tree for a live-rig gain plugin.
    \return Tracktion ValueTree containing the private plugin type.
    */
    [[nodiscard]] static juce::ValueTree createState();

    /*!
    \brief Creates a live-rig gain plugin from Tracktion plugin creation info.
    \param info Tracktion plugin creation context and state tree.
    */
    explicit LiveRigGainPlugin(tracktion::PluginCreationInfo info);

    /*! \brief Notifies Tracktion listeners that the plugin is being destroyed. */
    ~LiveRigGainPlugin() override;

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
    \brief Prevents the private structural plugin from being added to racks.
    \return Always false.
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
    \brief Applies the current gain to the render buffer.
    \param context Tracktion render block containing the audio buffer to scale.
    */
    void applyToBuffer(const tracktion::PluginRenderContext& context) override;

    /*!
    \brief Restores gain state from a Tracktion ValueTree.
    \param tree Tracktion state tree containing persisted gain properties.
    */
    void restorePluginStateFromValueTree(const juce::ValueTree& tree) override;

    /*!
    \brief Stores a clamped gain value for future processing blocks.
    \param gain Desired project-owned gain value.
    */
    void setGain(Gain gain);

    /*!
    \brief Returns the currently stored clamped gain value.
    \return Current clamped gain value.
    */
    [[nodiscard]] Gain gain() const noexcept;

private:
    [[nodiscard]] float targetLinearGain() const noexcept;
    void setTargetGainDb(float gain_db) noexcept;
    void setSmoothedGainTarget(float linear_gain) noexcept;

    juce::CachedValue<float> m_gain_db;
    std::atomic<float> m_target_gain_db{static_cast<float>(defaultGainDb())};
    juce::SmoothedValue<float> m_smoothed_gain{1.0f};
    float m_last_target_linear_gain{1.0f};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LiveRigGainPlugin)
};

} // namespace rock_hero::common::audio
