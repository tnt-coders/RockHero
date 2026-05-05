/*!
\file audio_clip_view.h
\brief JUCE component that renders one audio clip waveform.
*/

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>
#include <optional>
#include <rock_hero/ui/audio_clip_view_state.h>

namespace rock_hero::audio
{
// Forward-declared IThumbnail port keeps this header independent of Tracktion implementation types.
class IThumbnail;
} // namespace rock_hero::audio

namespace rock_hero::ui
{

/*!
\brief Renders one audio clip from view-derived clip state.

AudioClipView owns the Tracktion-free audio::IThumbnail used to render source waveform data. Track
rows own and lay out clip views, while each clip view keeps thumbnail source refresh local to the
asset it renders.
*/
class AudioClipView final : public juce::Component
{
public:
    /*! \brief Creates an empty clip view with no thumbnail installed yet. */
    AudioClipView();

    /*! \brief Releases the owned thumbnail before the JUCE component base is destroyed. */
    ~AudioClipView() override;

    /*!
    \brief Copying is disabled because JUCE components and thumbnail ownership are not copyable.
    */
    AudioClipView(const AudioClipView&) = delete;

    /*!
    \brief Copy assignment is disabled because JUCE components and thumbnail ownership are not
    copyable.
    */
    AudioClipView& operator=(const AudioClipView&) = delete;

    /*! \brief Moving is disabled because JUCE components and thumbnail ownership are fixed. */
    AudioClipView(AudioClipView&&) = delete;

    /*!
    \brief Move assignment is disabled because JUCE components and thumbnail ownership are fixed.
    */
    AudioClipView& operator=(AudioClipView&&) = delete;

    /*!
    \brief Installs the clip-owned thumbnail renderer.
    \param thumbnail Newly created thumbnail owned by this view.
    */
    void setThumbnail(std::unique_ptr<audio::IThumbnail> thumbnail);

    /*!
    \brief Applies the current audio clip state.
    \param state New audio clip state to render.
    */
    void setState(const AudioClipViewState& state);

    /*!
    \brief Draws the clip-local waveform content for the current state.
    \param g Graphics context used for drawing.
    */
    void paint(juce::Graphics& g) override;

private:
    // Refreshes the owned thumbnail only when the clip now points at a different asset.
    void applyCurrentAssetToThumbnailIfNeeded();

    // Current clip state last applied by the owning track view.
    AudioClipViewState m_state{};

    // Asset currently installed into the owned thumbnail, if any.
    std::optional<core::AudioAsset> m_thumbnail_source_asset{};

    // Clip-view-owned thumbnail used to render static waveform content.
    std::unique_ptr<audio::IThumbnail> m_thumbnail;
};

} // namespace rock_hero::ui
