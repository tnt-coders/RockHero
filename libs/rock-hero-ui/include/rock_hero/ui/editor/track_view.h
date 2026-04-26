/*!
\file track_view.h
\brief JUCE component that renders one track view.
*/

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>
#include <optional>
#include <rock_hero/ui/editor/track_view_state.h>

namespace rock_hero::audio
{
// Forward-declared thumbnail port keeps this header independent of Tracktion implementation types.
class Thumbnail;
} // namespace rock_hero::audio

namespace rock_hero::ui
{

/*!
\brief Renders one track view from framework-free track state.

The view owns one Tracktion-free audio::Thumbnail and refreshes it only when the incoming
TrackViewState changes to a different present audio asset. It currently renders only track-local
waveform content and emits normalized click intent upward. Cursor motion is intentionally
excluded; the editor-wide cursor overlay arrives later in stage 10.
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

    /*! \brief Creates an empty track view with no thumbnail installed yet. */
    TrackView();

    /*! \brief Releases track-view-owned thumbnail resources. */
    ~TrackView() override;

    /*! \brief Copying is disabled because JUCE components and thumbnail ownership are not copyable.
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
    \brief Installs the track-view-owned thumbnail renderer.

    If the view already holds a present audio asset in its current state, the new thumbnail is
    immediately pointed at that asset before this method returns.

    \param thumbnail Newly created thumbnail owned by this view.
    */
    void setThumbnail(std::unique_ptr<audio::Thumbnail> thumbnail);

    /*!
    \brief Applies the current framework-free track-view state.

    If the state changes to a different present audio asset, the view refreshes its owned thumbnail
    source exactly once for that asset.

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
    \brief Draws the track-local waveform content for the current state.
    \param g Graphics context used for drawing.
    */
    void paint(juce::Graphics& g) override;

    /*! \brief Responds to track view size changes. */
    void resized() override;

private:
    // Refreshes the owned thumbnail only when the view now points at a different present asset.
    void applyCurrentAssetToThumbnailIfNeeded();

    // Current framework-free track-view state last applied by the parent view.
    TrackViewState m_state{};

    // Asset currently installed into the owned thumbnail, if any.
    std::optional<core::AudioAsset> m_thumbnail_source_asset{};

    // Track-view-owned thumbnail used to render static waveform content.
    std::unique_ptr<audio::Thumbnail> m_thumbnail;

    // Local listeners notified when the track view is clicked.
    juce::ListenerList<Listener> m_listeners;
};

} // namespace rock_hero::ui
