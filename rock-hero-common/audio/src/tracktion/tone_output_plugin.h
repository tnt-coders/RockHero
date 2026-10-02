/*!
\file tone_output_plugin.h
\brief Private Tracktion plugin applying one tone's output stage: automatable level and pan.
*/

#pragma once

#include <array>
#include <rock_hero/common/audio/shared/tone_output.h>
#include <tracktion_engine/tracktion_engine.h>

namespace rock_hero::common::audio
{

/*!
\brief Hidden Tracktion plugin that applies a tone's authored level and pan after its chain.

The tone's own output stage, locked in the branch's last user-facing slot ahead of the branch's
switch gain: level and pan are the two real automatable parameters a Volume or Pan lane drives, and
nothing else is reachable here, so no lane can ever touch the tone-switch crossfade that
\ref ToneBranchGainPlugin owns. Gains in series multiply, so a switch still follows the tone's
level.

Both parameters keep their stored value as the parameter's explicit value, never an attached
`CachedValue`: Tracktion writes a playing curve's value back into an attached value, which would
make a save mid-playback persist the curve's value instead of the authored one. The pan is a
balance with unity at centre (`juce::dsp::PannerRule::balanced`): the near channel stays at unity
and only the far channel attenuates, so a stereo signal keeps its width.
*/
class ToneOutputPlugin final : public tracktion::Plugin
{
public:
    /*! \brief Tracktion plugin type stored in the plugin ValueTree. */
    static const char* xmlTypeName;

    /*! \brief Parameter id of the level parameter, the Volume lane's `param_id`. */
    static constexpr const char* g_level_parameter_id{"level"};

    /*! \brief Parameter id of the pan parameter, the Pan lane's `param_id`. */
    static constexpr const char* g_pan_parameter_id{"pan"};

    /*!
    \brief Creates the minimal Tracktion state tree for a tone output plugin.
    \return Tracktion ValueTree containing the private plugin type.
    */
    [[nodiscard]] static juce::ValueTree createState();

    /*!
    \brief Creates a tone output plugin from Tracktion plugin creation info.
    \param info Tracktion plugin creation context and state tree.
    */
    explicit ToneOutputPlugin(tracktion::PluginCreationInfo info);

    /*! \brief Notifies Tracktion listeners of destruction. */
    ~ToneOutputPlugin() override;

    /*! \brief Moving is disabled; Tracktion plugins are reference-counted graph nodes. */
    ToneOutputPlugin(ToneOutputPlugin&&) = delete;

    /*!
    \brief Move assignment is disabled; Tracktion plugins are reference-counted graph nodes.
    \return Reference to this plugin.
    */
    ToneOutputPlugin& operator=(ToneOutputPlugin&&) = delete;

    /*!
    \brief Returns the user-visible plugin name, which the automation picker shows as the stage.
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
    \brief Prepares the per-channel smoothers from Tracktion playback settings.
    \param info Tracktion playback initialization context.
    */
    void initialise(const tracktion::PluginInitialisationInfo& info) override;

    /*!
    \brief Handles Tracktion graph reuse without touching realtime state.
    \param info Tracktion playback initialization context.
    */
    void initialiseWithoutStopping(const tracktion::PluginInitialisationInfo& info) override;

    /*! \brief Releases playback resources. */
    void deinitialise() override;

    /*!
    \brief Applies the current level and pan to the render buffer's stereo pair.
    \param context Tracktion render block containing the audio buffer to scale.
    */
    void applyToBuffer(const tracktion::PluginRenderContext& context) override;

    /*!
    \brief Stores the tone's authored output stage as both parameters' explicit values.
    \param output Desired level and pan; clamped to the accepted ranges.
    */
    void setOutput(ToneOutput output);

    /*!
    \brief Returns the tone's authored output stage, independent of any playing curve.

    Both values round through the parameters' `float` storage, so a set value reads back at float
    precision (the level already did on its previous `atomic<float>` home).

    \return Current clamped level and pan.
    */
    [[nodiscard]] ToneOutput output() const noexcept;

private:
    tracktion::AutomatableParameter::Ptr m_level_parameter;
    tracktion::AutomatableParameter::Ptr m_pan_parameter;

    // One de-zipper per channel of the stereo pair; the rack is stereo throughout.
    std::array<juce::SmoothedValue<float>, 2> m_smoothed_channel_gains{};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ToneOutputPlugin)
};

} // namespace rock_hero::common::audio
