/*!
\file recording_thumbnail.h
\brief Recording audio-thumbnail test implementations.
*/

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>
#include <optional>
#include <rock_hero/common/audio/i_thumbnail.h>
#include <rock_hero/common/audio/i_thumbnail_factory.h>
#include <vector>

namespace rock_hero::common::audio::testing
{

/*!
\brief IThumbnail implementation that records source and draw requests.

Use this when UI tests need to observe thumbnail source assignment, proxy state reads, or paint
requests without constructing the concrete Tracktion-backed thumbnail adapter.
*/
class RecordingThumbnail final : public IThumbnail
{
public:
    /*!
    \brief Records the source asset and marks the thumbnail as drawable.
    \param audio_asset Audio asset assigned by the component under test.
    */
    void setSource(const common::core::AudioAsset& audio_asset) override
    {
        last_source = audio_asset;
        has_source = true;
        set_source_call_count += 1;
    }

    /*!
    \brief Reports whether setSource() has supplied drawable source data.
    \return True after a source asset has been assigned.
    */
    [[nodiscard]] bool hasSource() const override
    {
        return has_source;
    }

    /*!
    \brief Reports the configured proxy-generation state.
    \return Configured proxy-generation state.
    */
    [[nodiscard]] bool isGeneratingProxy() const override
    {
        return generating_proxy;
    }

    /*!
    \brief Reports the configured proxy-generation progress.
    \return Configured proxy progress fraction.
    */
    [[nodiscard]] float getProxyProgress() const override
    {
        return proxy_progress;
    }

    /*!
    \brief Records draw parameters and returns the configured draw outcome.
    \param bounds Target drawing bounds requested by the component under test.
    \param visible_range Timeline range requested for rendering.
    \param vertical_zoom Vertical waveform scale requested for rendering.
    \return Configured draw result.
    */
    [[nodiscard]] bool drawChannels(
        juce::Graphics& g, juce::Rectangle<int> bounds, common::core::TimeRange visible_range,
        float vertical_zoom) override
    {
        last_draw_bounds = bounds;
        last_drawn_visible_range = visible_range;
        last_vertical_zoom = vertical_zoom;
        if (fill_color.has_value())
        {
            g.setColour(*fill_color);
            g.fillRect(bounds);
        }
        return draw_result;
    }

    /*! \brief Last asset assigned through setSource(). */
    std::optional<common::core::AudioAsset> last_source{};

    /*! \brief Last visible timeline range requested during paint. */
    std::optional<common::core::TimeRange> last_drawn_visible_range{};

    /*! \brief Last target bounds requested during paint. */
    std::optional<juce::Rectangle<int>> last_draw_bounds{};

    /*! \brief Last vertical zoom requested during paint. */
    std::optional<float> last_vertical_zoom{};

    /*! \brief Optional solid color drawn into the requested waveform bounds during tests. */
    std::optional<juce::Colour> fill_color{};

    /*! \brief Number of source assignments received. */
    int set_source_call_count{0};

    /*! \brief Proxy-generation state returned by isGeneratingProxy(). */
    bool generating_proxy{false};

    /*! \brief Proxy progress returned by getProxyProgress(). */
    float proxy_progress{0.0f};

    /*! \brief Source-readiness flag returned by hasSource(). */
    bool has_source{false};

    /*! \brief Draw result returned by drawChannels(). */
    bool draw_result{true};
};

/*!
\brief IThumbnailFactory implementation that creates RecordingThumbnail instances.

The factory stores non-owning handles to thumbnails it has returned so tests can inspect the
thumbnail after ownership has moved into the component under test.
*/
class RecordingThumbnailFactory final : public IThumbnailFactory
{
public:
    /*!
    \brief Creates a recording thumbnail and records the requesting owner component.
    \param owner Component that will own or display the returned thumbnail.
    \return Newly created recording thumbnail through the thumbnail interface.
    */
    [[nodiscard]] std::unique_ptr<IThumbnail> createThumbnail(juce::Component& owner) override
    {
        last_owner = &owner;
        create_call_count += 1;
        auto thumbnail = std::make_unique<RecordingThumbnail>();
        last_thumbnail = thumbnail.get();
        thumbnails.push_back(thumbnail.get());
        return thumbnail;
    }

    /*! \brief Last component that requested a thumbnail. */
    juce::Component* last_owner{nullptr};

    /*! \brief Last thumbnail returned to a component. */
    RecordingThumbnail* last_thumbnail{nullptr};

    /*! \brief Non-owning handles to thumbnails created during the current test. */
    std::vector<RecordingThumbnail*> thumbnails{};

    /*! \brief Number of thumbnails created by the factory. */
    int create_call_count{0};
};

} // namespace rock_hero::common::audio::testing
