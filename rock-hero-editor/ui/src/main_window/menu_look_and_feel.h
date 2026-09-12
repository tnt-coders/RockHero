/*!
\file menu_look_and_feel.h
\brief Flat application-chrome look-and-feel for the editor menu bar.
*/

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace rock_hero::editor::ui
{

/*! \brief Paints the editor menu strip as flat application chrome instead of a framed control. */
class MenuLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    /*!
    \brief Binds the look-and-feel to the one menu bar it paints.

    The bar is held so the access-key setter can invalidate it; a JUCE look-and-feel is otherwise
    free to be shared, but this one paints exactly the editor's menu strip.

    \param menu_bar Menu bar this look-and-feel paints and repaints on an access-key change.
    */
    explicit MenuLookAndFeel(juce::MenuBarComponent& menu_bar);

    /*!
    \brief Shows or hides the underlined access letter in every menu title; repaints on a change.

    Windows hides menu mnemonics until Alt is held, so the editor pushes the same held-Alt
    predicate that drives the tab lane's actual-ring reveal in here once per frame. While it is on,
    each menu title underlines its first character, which is the letter its Alt chord matches.

    \param visible True while Alt is held with this application in the foreground.
    */
    void setAccessKeysVisible(bool visible);

    /*!
    \brief Matches the editor background without the default JUCE top/bottom border lines.

    \param g Graphics context used for drawing.
    \param width Menu bar width in pixels.
    \param height Menu bar height in pixels.
    \param is_mouse_over_bar True while the mouse is over the menu bar.
    \param menu_bar Menu bar component being painted.
    */
    void drawMenuBarBackground(
        juce::Graphics& g, int width, int height, bool is_mouse_over_bar,
        juce::MenuBarComponent& menu_bar) override;

    /*!
    \brief Keeps the menu item readable on the flat strip and uses a simple hover fill.

    \param g Graphics context used for drawing.
    \param width Menu item width in pixels.
    \param height Menu item height in pixels.
    \param item_index Index of the menu item being painted.
    \param item_text Menu item title text.
    \param is_mouse_over_item True while the mouse is over this menu item.
    \param is_menu_open True while this item's popup menu is open.
    \param is_mouse_over_bar True while the mouse is over the menu bar.
    \param menu_bar Menu bar component being painted.
    */
    void drawMenuBarItem(
        juce::Graphics& g, int width, int height, int item_index, const juce::String& item_text,
        bool is_mouse_over_item, bool is_menu_open, bool is_mouse_over_bar,
        juce::MenuBarComponent& menu_bar) override;

private:
    // The strip this look-and-feel paints, held so an access-key change can invalidate it.
    juce::MenuBarComponent& m_menu_bar;

    // True while the held-Alt hint is showing: every menu title underlines its access letter.
    bool m_access_keys_visible{false};
};

} // namespace rock_hero::editor::ui
