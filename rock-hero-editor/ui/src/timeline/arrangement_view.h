/*!
\file arrangement_view.h
\brief JUCE component that renders the current arrangement view.
*/

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>
#include <optional>
#include <rock_hero/common/core/audio_asset.h>
#include <rock_hero/common/core/timeline.h>
#include <rock_hero/editor/core/timeline/arrangement_view_state.h>

namespace rock_hero::common::audio
{
// Forward-declared thumbnail port keeps this header independent of concrete audio adapters.
class IThumbnail;

// Forward-declared thumbnail factory keeps this header independent of concrete audio adapters.
class IThumbnailFactory;
} // namespace rock_hero::common::audio

namespace rock_hero::editor::ui
{

/*!
\brief Renders the current arrangement from framework-free state.

The view owns one thumbnail renderer for the arrangement's full-source audio and draws the
currently visible timeline range. Cursor and tempo-grid overlays are intentionally excluded;
EditorView owns editor-wide timeline overlays.
*/
class ArrangementView : public juce::Component
{
public:
    /*!
    \brief Listener for local arrangement-view click intent.

    The view stays presentation-focused. It reports normalized horizontal click positions and
    leaves all seek policy to its parent.
    */
    class Listener
    {
    public:
        /*! \brief Allows cleanup through a base pointer. */
        virtual ~Listener() = default;

        /*!
        \brief Reports a click within the arrangement view as a normalized horizontal position.
        \param view Arrangement view component that was clicked.
        \param normalized_x Click position normalized to the interval [0, 1].
        */
        virtual void arrangementViewClicked(ArrangementView& view, double normalized_x) = 0;

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

    /*! \brief Creates an empty arrangement view with no thumbnail factory installed yet. */
    ArrangementView();

    /*! \brief Releases arrangement-view-owned thumbnail and listener state. */
    ~ArrangementView() override;

    /*!
    \brief Copying is disabled because JUCE components and thumbnail ownership are not copyable.
    */
    ArrangementView(const ArrangementView&) = delete;

    /*!
    \brief Copy assignment is disabled because JUCE components and thumbnail ownership are not
    copyable.
    */
    ArrangementView& operator=(const ArrangementView&) = delete;

    /*!
    \brief Moving is disabled because JUCE components and listener registrations are not movable.
    */
    ArrangementView(ArrangementView&&) = delete;

    /*!
    \brief Move assignment is disabled because JUCE components and listener registrations are not
    movable.
    */
    ArrangementView& operator=(ArrangementView&&) = delete;

    /*!
    \brief Installs the factory used to create the arrangement-owned thumbnail renderer.

    The thumbnail is bound to this component so proxy completion can repaint the arrangement view.

    \param thumbnail_factory Factory used for the arrangement thumbnail.
    */
    void setThumbnailFactory(common::audio::IThumbnailFactory& thumbnail_factory);

    /*!
    \brief Applies the visible timeline range used to choose the waveform span.
    \param visible_timeline Timeline range currently visible in the editor.
    */
    void setVisibleTimeline(common::core::TimeRange visible_timeline);

    /*!
    \brief Applies the current framework-free arrangement-view state.

    The view refreshes its thumbnail source when the assigned arrangement audio changes.

    \param state New arrangement-view state to render.
    */
    void setState(const core::ArrangementViewState& state);

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
    \param event JUCE mouse event relative to this arrangement view.
    */
    void mouseDown(const juce::MouseEvent& event) override;

    /*!
    \brief Draws empty states and waveform content.
    \param g Graphics context used for drawing.
    */
    void paint(juce::Graphics& g) override;

    /*! \brief Invalidates waveform content after arrangement view size changes. */
    void resized() override;

private:
    // Refreshes the owned thumbnail only when the arrangement now points at a different asset.
    void applyCurrentAudioToThumbnailIfNeeded();

    // Current framework-free arrangement-view state last applied by the parent view.
    core::ArrangementViewState m_state{};

    // Editor-visible timeline range used to choose the waveform span.
    common::core::TimeRange m_visible_timeline{};

    // Arrangement-view-owned thumbnail used to render static waveform content.
    std::unique_ptr<common::audio::IThumbnail> m_thumbnail;

    // Asset currently installed into the owned thumbnail, if any.
    std::optional<common::core::AudioAsset> m_thumbnail_source_asset{};

    // Local listeners notified when the arrangement view is clicked.
    juce::ListenerList<Listener> m_listeners;
};

} // namespace rock_hero::editor::ui
