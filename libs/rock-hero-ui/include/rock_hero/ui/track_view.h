/*!
\file track_view.h
\brief JUCE component that renders one track view.
*/

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>
#include <rock_hero/core/timeline.h>
#include <rock_hero/ui/track_view_state.h>
#include <vector>

namespace rock_hero::audio
{
// Forward-declared thumbnail factory keeps this header independent of concrete audio adapters.
class IThumbnailFactory;
} // namespace rock_hero::audio

namespace rock_hero::ui
{

class AudioClipView;

/*!
\brief Renders one track view from framework-free track state.

The view owns clip child components and maps their timeline ranges into the editor's visible
timeline range. Cursor motion is intentionally excluded; EditorView owns the editor-wide cursor
overlay.
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

    /*! \brief Releases track-view-owned clip views and listener state. */
    ~TrackView() override;

    /*!
    \brief Copying is disabled because JUCE components and clip ownership are not copyable.
    */
    TrackView(const TrackView&) = delete;

    /*!
    \brief Copy assignment is disabled because JUCE components and clip ownership are not copyable.
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
    \brief Installs the factory used to create clip-owned thumbnail renderers.

    Existing clip views are rebuilt because their thumbnails are bound to their owning clip
    components at creation time.

    \param thumbnail_factory Factory used for current and future clip views.
    */
    void setThumbnailFactory(audio::IThumbnailFactory& thumbnail_factory);

    /*!
    \brief Applies the visible timeline range used to map clips into row coordinates.
    \param visible_timeline Timeline range currently visible in the editor.
    */
    void setVisibleTimeline(core::TimeRange visible_timeline);

    /*!
    \brief Applies the current framework-free track-view state.

    The row creates or updates child clip views from the supplied clip state.

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
    \brief Draws row background and empty-state text behind clip children.
    \param g Graphics context used for drawing.
    */
    void paint(juce::Graphics& g) override;

    /*! \brief Responds to track view size changes. */
    void resized() override;

private:
    // Rebuilds and updates child clip views to match the current row state.
    void syncClipViewsToState();

    // Detaches and releases clip child components before rebuilding or teardown.
    void clearClipViews();

    // Current framework-free track-view state last applied by the parent view.
    TrackViewState m_state{};

    // Editor-visible timeline range used to map clip timeline ranges to child bounds.
    core::TimeRange m_visible_timeline{};

    // Factory that creates thumbnails bound to each owned clip view.
    audio::IThumbnailFactory* m_thumbnail_factory{nullptr};

    // Clip-view children owned and laid out by this track row.
    std::vector<std::unique_ptr<AudioClipView>> m_clip_views;

    // Local listeners notified when the track view is clicked.
    juce::ListenerList<Listener> m_listeners;
};

} // namespace rock_hero::ui
