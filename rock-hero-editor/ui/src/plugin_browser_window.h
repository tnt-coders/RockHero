/*!
\file plugin_browser_window.h
\brief JUCE window that displays scanned plugins and emits browser intents.
*/

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>
#include <optional>
#include <rock_hero/editor/core/busy_view_state.h>
#include <rock_hero/editor/core/plugin_browser_view_state.h>
#include <string>

namespace rock_hero::editor::ui
{

/*!
\brief Top-level plugin browser window.

The window renders controller-derived plugin catalog state and emits plain browser intents. It
does not scan plugins, choose plugin paths, or mutate the signal chain directly.
*/
class PluginBrowserWindow final : public juce::DocumentWindow
{
public:
    /*! \brief Listener that receives browser window intents. */
    class Listener
    {
    public:
        /*! \brief Destroys the listener interface. */
        virtual ~Listener() = default;

        /*! \brief Requests a plugin catalog rescan. */
        virtual void onPluginBrowserScanRequested() = 0;

        /*!
        \brief Requests that the selected browser plugin be added.
        \param plugin_id Opaque plugin ID selected by the user.
        */
        virtual void onPluginBrowserAddRequested(std::string plugin_id) = 0;

        /*! \brief Reports that the browser window was closed. */
        virtual void onPluginBrowserClosed() = 0;

        /*! \brief Requests cancellation of the active editor-wide busy operation. */
        virtual void onPluginBrowserBusyCancelRequested() = 0;

    protected:
        /*! \brief Creates the listener interface. */
        Listener() = default;

        /*! \brief Copies the listener interface. */
        Listener(const Listener&) = default;

        /*! \brief Moves the listener interface. */
        Listener(Listener&&) = default;

        /*!
        \brief Assigns the listener interface from another interface.
        \return Reference to this listener interface.
        */
        Listener& operator=(const Listener&) = default;

        /*!
        \brief Move-assigns the listener interface from another interface.
        \return Reference to this listener interface.
        */
        Listener& operator=(Listener&&) = default;
    };

    /*!
    \brief Creates the plugin browser window.
    \param listener Listener that receives browser intents.
    */
    explicit PluginBrowserWindow(Listener& listener);

    /*! \brief Releases browser content. */
    ~PluginBrowserWindow() override;

    /*! \brief Copying is disabled because JUCE window ownership is not copyable. */
    PluginBrowserWindow(const PluginBrowserWindow&) = delete;

    /*! \brief Copy assignment is disabled because JUCE window ownership is not copyable. */
    PluginBrowserWindow& operator=(const PluginBrowserWindow&) = delete;

    /*! \brief Moving is disabled because JUCE component ownership is not movable. */
    PluginBrowserWindow(PluginBrowserWindow&&) = delete;

    /*! \brief Move assignment is disabled because JUCE component ownership is not movable. */
    PluginBrowserWindow& operator=(PluginBrowserWindow&&) = delete;

    /*!
    \brief Applies controller-derived browser state to the window content.
    \param state Plugin browser state to render.
    */
    void setState(const core::PluginBrowserViewState& state);

    /*!
    \brief Applies editor-wide busy state over the browser content.
    \param busy Busy state to render, or empty when the browser should be interactive.
    */
    void setBusyState(const std::optional<core::BusyViewState>& busy);

    /*! \brief Forwards native close-button clicks as a controller intent. */
    void closeButtonPressed() override;

private:
    class Content;

    // Listener that receives browser intents emitted by the content and window close button.
    Listener& m_listener;

    // Owned content component installed into the DocumentWindow.
    std::unique_ptr<Content> m_content;
};

} // namespace rock_hero::editor::ui
