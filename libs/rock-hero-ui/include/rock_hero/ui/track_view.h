/*!
\file track_view.h
\brief JUCE component that renders one track view.
*/

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>
#include <optional>
#include <rock_hero/core/audio_asset.h>
#include <rock_hero/core/timeline.h>
#include <rock_hero/ui/track_view_state.h>

namespace rock_hero::audio
{
// Forward-declared thumbnail port keeps this header independent of concrete audio adapters.
class IThumbnail;

// Forward-declared thumbnail factory keeps this header independent of concrete audio adapters.
class IThumbnailFactory;
} // namespace rock_hero::audio

namespace rock_hero::ui
{

/*!
\brief Renders one track view from framework-free track state.

The view owns one thumbnail renderer for the track's full-source audio and draws the currently
visible timeline range. Cursor motion is intentionally excluded; EditorView owns the editor-wide
cursor overlay.
*/
class TrackView : public juce::Component
{
public:
    /*!
    \brief Listener for local track-view click intent.

    The view stays presentation-focused. It reports normalized horizontal click positions and
    leaves all seek policy to its parent.
    */
    class Listener
    {
    public:
        /*! \brief Allows cleanup through a base pointer. */
        virtual ~Listener() = default;

        /*!
        \brief Reports a click within the track view as a normalized horizontal position.
        \param view Track view component that was clicked.
        \param normalized_x Click position normalized to the interval [0, 1].
        */
        virtual void trackViewClicked(TrackView& view, double normalized_x) = 0;

    protected:
        /*! \brief Constructs the listener base. */
        Listener() = default;

        /*! \brief Copies the listener base for derived listener types. */
        Listener(const Listener&) = default;

        /*! \brief Moves the listener base for derived listener types. */
        Listener(Listener&&) = default;

        /*!
        \brief Copies the listener base for derived listener types.
        \return This listener base.
        */
        Listener& operator=(const Listener&) = default;

        /*!
        \brief Moves the listener base for derived listener types.
        \return This listener base.
        */
        Listener& operator=(Listener&&) = default;
    };

    /*! \brief Creates an empty track view with no thumbnail factory installed yet. */
    TrackView();

    /*! \brief Releases track-view-owned thumbnail and listener state. */
    ~TrackView() override;

    /*!
    \brief Copying is disabled because JUCE components and thumbnail ownership are not copyable.
    */
    TrackView(const TrackView&) = delete;

    /*!
    \brief Copy assignment is disabled because JUCE components and thumbnail ownership are not
    copyable.
    */
    TrackView& operator=(const TrackView&) = delete;

    /*!
    \brief Moving is disabled because JUCE components and listener registrations are not movable.
    */
    TrackView(TrackView&&) = delete;

    /*!
    \brief Move assignment is disabled because JUCE components and listener registrations are not
    movable.
    */
    TrackView& operator=(TrackView&&) = delete;

    /*!
    \brief Installs the factory used to create the track-owned thumbnail renderer.

    The thumbnail is bound to this component so proxy completion can repaint the track row.

    \param thumbnail_factory Factory used for the track thumbnail.
    */
    void setThumbnailFactory(audio::IThumbnailFactory& thumbnail_factory);

    /*!
    \brief Applies the visible timeline range used to choose the waveform span.
    \param visible_timeline Timeline range currently visible in the editor.
    */
    void setVisibleTimeline(core::TimeRange visible_timeline);

    /*!
    \brief Applies the current framework-free track-view state.

    The row refreshes its thumbnail source when the assigned track audio changes.

    \param state New track-view state to render.
    */
    void setState(const TrackViewState& state);

    /*!
    \brief Adds a local click listener.
    \param listener Listener to notify until it is removed.
    */
    void addListener(Listener& listener);

    /*!
    \brief Removes a previously added click listener.
    \param listener Same listener previously passed to addListener().
    */
    void removeListener(Listener& listener);

    /*!
    \brief Emits normalized click intent for the parent view layer.
    \param event JUCE mouse event relative to this track view.
    */
    void mouseDown(const juce::MouseEvent& event) override;

    /*!
    \brief Draws row background, empty states, and waveform content.
    \param g Graphics context used for drawing.
    */
    void paint(juce::Graphics& g) override;

    /*! \brief Invalidates waveform content after track view size changes. */
    void resized() override;

private:
    // Refreshes the owned thumbnail only when the track now points at a different asset.
    void applyCurrentAudioToThumbnailIfNeeded();

    // Current framework-free track-view state last applied by the parent view.
    TrackViewState m_state{};

    // Editor-visible timeline range used to choose the waveform span.
    core::TimeRange m_visible_timeline{};

    // Track-view-owned thumbnail used to render static waveform content.
    std::unique_ptr<audio::IThumbnail> m_thumbnail;

    // Asset currently installed into the owned thumbnail, if any.
    std::optional<core::AudioAsset> m_thumbnail_source_asset{};

    // Local listeners notified when the track view is clicked.
    juce::ListenerList<Listener> m_listeners;
};

} // namespace rock_hero::ui
