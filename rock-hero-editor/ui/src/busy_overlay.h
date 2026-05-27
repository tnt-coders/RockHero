/*!
\file busy_overlay.h
\brief Editor-wide busy overlay rendered above editor content during slow operations.
*/

#pragma once

#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>
#include <optional>
#include <rock_hero/editor/core/busy_view_state.h>

namespace rock_hero::editor::ui
{

/*!
\brief Translucent overlay shown over the editor while a slow operation is in flight.

BusyOverlay is a presentation-only child of EditorView. It renders indeterminate progress,
determinate percentage progress, or static blocking text with the message supplied by
BusyViewState, dims the editor underneath, and intercepts mouse and keyboard input so the user
cannot drive disabled editor commands through the EditorView child component tree. It is not a
JUCE modal component; its visibility is driven entirely by EditorViewState::busy.
*/
class BusyOverlay final : public juce::Component
{
public:
    /*! \brief Creates the overlay in a hidden, non-intercepting state. */
    BusyOverlay();

    /*! \brief Releases child widgets owned by the overlay. */
    ~BusyOverlay() override = default;

    /*! \brief Copying is disabled because JUCE component ownership is not copyable. */
    BusyOverlay(const BusyOverlay&) = delete;

    /*! \brief Copy assignment is disabled because JUCE component ownership is not copyable. */
    BusyOverlay& operator=(const BusyOverlay&) = delete;

    /*! \brief Moving is disabled because child component registrations are not movable. */
    BusyOverlay(BusyOverlay&&) = delete;

    /*! \brief Move assignment is disabled because child component registrations are not movable. */
    BusyOverlay& operator=(BusyOverlay&&) = delete;

    /*!
    \brief Applies the latest busy state.

    When busy carries a value, the overlay becomes visible, displays the supplied message, and
    grabs keyboard focus so editor shortcuts stop reaching the EditorView component tree. When
    busy is empty, the overlay hides and stops intercepting input. The view rendering this
    overlay must keep it as the front-most child so the dim layer paints above other editor
    content.

    \param busy Latest controller-derived busy state, or empty when no operation is active.
    */
    void setBusyState(const std::optional<core::BusyViewState>& busy);

    /*!
    \brief Installs a callback fired after the overlay paints its busy surface.

    EditorView uses this as a paint fence for message-thread-only work. The callback may fire on
    every busy overlay paint; the owner remains responsible for making any operation callback
    single-shot.

    \param callback Callback notified from paint().
    */
    void setPaintCallback(std::function<void()> callback);

    /*!
    \brief Paints the dim layer and centered progress surface.
    \param g Graphics context used for drawing.
    */
    void paint(juce::Graphics& g) override;

    /*! \brief Lays out the centered progress surface, progress bar, and message label. */
    void resized() override;

    /*!
    \brief Consumes keyboard input while busy so editor shortcuts cannot reach EditorView.
    \param key Key press delivered by JUCE.
    \return Always true; the overlay swallows every key while visible.
    */
    bool keyPressed(const juce::KeyPress& key) override;

private:
    // JUCE renders ProgressBar in indeterminate mode when the backing value is negative. A
    // non-negative value renders determinate percentage progress.
    double m_progress{-1.0};

    // Progress indicator rendered above the message when the busy state requests it.
    juce::ProgressBar m_progress_bar{m_progress};

    // User-facing message supplied by BusyViewState::message.
    juce::Label m_message_label;

    // Optional owner callback used by EditorView to implement its busy-overlay paint fence.
    std::function<void()> m_paint_callback;
};

} // namespace rock_hero::editor::ui
