/*!
\file transport_controls.h
\brief Play/pause/stop transport button strip.
*/

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>
#include <rock_hero/ui/transport_controls_state.h>

namespace rock_hero::ui
{

/*!
\brief A horizontal strip containing Play/Pause and Stop transport buttons.

Interaction is entirely listener-based; this component has no knowledge of transport policy or the
audio engine. The owner supplies already-derived TransportControlsState and handles emitted
play/pause and stop intents through Listener.

Icons are SVGs embedded via juce_add_binary_data (BinaryData::play_arrow_svg, pause_svg,
stop_svg). Buttons use DrawableButton with ImageFitted style.
*/
class TransportControls : public juce::Component
{
public:
    /*!
    \brief Receives transport intents emitted by the child buttons.
    */
    class Listener
    {
    public:
        /*! \brief Destroys the local listener interface. */
        virtual ~Listener() = default;

        /*! \brief Handles a play/pause intent from the primary transport button. */
        virtual void onPlayPausePressed() = 0;

        /*! \brief Handles a stop intent from the secondary transport button. */
        virtual void onStopPressed() = 0;

    protected:
        /*! \brief Creates the local listener interface. */
        Listener() = default;

        /*! \brief Copies the local listener interface. */
        Listener(const Listener&) = default;

        /*! \brief Moves the local listener interface. */
        Listener(Listener&&) = default;

        /*!
        \brief Assigns the local listener interface from another interface.
        \return Reference to this local listener interface.
        */
        Listener& operator=(const Listener&) = default;

        /*!
        \brief Move-assigns the local listener interface from another interface.
        \return Reference to this local listener interface.
        */
        Listener& operator=(Listener&&) = default;
    };

    /*!
    \brief Creates icon-backed transport buttons bound to a required local listener.
    \param listener Parent listener that receives play/pause and stop intents.
    */
    explicit TransportControls(Listener& listener);

    /*! \brief Releases button and icon resources. */
    ~TransportControls() override;

    /*! \brief Copying is disabled because JUCE components and listener state are not copyable. */
    TransportControls(const TransportControls&) = delete;

    /*! \brief Copy assignment is disabled because JUCE components and listener state are not
     * copyable. */
    TransportControls& operator=(const TransportControls&) = delete;

    /*! \brief Moving is disabled because JUCE components and listener state are not movable. */
    TransportControls(TransportControls&&) = delete;

    /*! \brief Move assignment is disabled because JUCE components and listener state are not
     * movable. */
    TransportControls& operator=(TransportControls&&) = delete;

    /*!
    \brief Applies a fully derived transport-controls rendering state.
    \param state Widget state to render.
    */
    void setState(const TransportControlsState& state);

    /*! \brief Lays out the transport buttons within the component bounds. */
    void resized() override;

private:
    // Emits the local play/pause intent without adding editor workflow policy to the widget.
    void handlePlayPauseClicked();

    // Emits the local stop intent without adding editor workflow policy to the widget.
    void handleStopClicked();

    // Parent listener that owns the actual transport semantics.
    Listener& m_listener;

    // Last state applied to the widget so repaint and debugging can reason about visible state.
    TransportControlsState m_state{};

    // Button that renders either Play or Pause based on m_state.
    std::unique_ptr<juce::DrawableButton> m_play_pause_button;

    // Button that sends Stop intent when playback or cursor state can be reset.
    std::unique_ptr<juce::DrawableButton> m_stop_button;

    // Embedded Play icon retained because DrawableButton stores non-owning image pointers.
    std::unique_ptr<juce::Drawable> m_play_drawable;

    // Embedded Pause icon retained because DrawableButton stores non-owning image pointers.
    std::unique_ptr<juce::Drawable> m_pause_drawable;

    // Embedded Stop icon retained because DrawableButton stores non-owning image pointers.
    std::unique_ptr<juce::Drawable> m_stop_drawable;
};

} // namespace rock_hero::ui
