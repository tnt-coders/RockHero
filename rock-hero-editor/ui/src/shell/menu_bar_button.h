/*!
\file menu_bar_button.h
\brief Right-aligned menu-bar action drawn as a peer to MenuBarComponent items.
*/

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace rock_hero::editor::ui
{

/*!
\brief Click-action button painted to match menu-bar item titles.

MenuBarButton sits in the same strip as juce::MenuBarComponent and reproduces its hover,
pressed, disabled, and text styling, but invokes a direct action on click instead of opening a
popup. It exists so right-side menu-bar actions, such as the audio-device selector, can be modeled
as menu-bar peers while still using juce::Button's input, focus, and accessibility behavior.
*/
class MenuBarButton final : public juce::Button
{
public:
    /*! \brief Creates an empty, enabled menu-bar action button. */
    MenuBarButton();

    /*! \brief Default destructor; no owned resources to release. */
    ~MenuBarButton() override = default;

    /*! \brief Copying is disabled because JUCE component ownership is not copyable. */
    MenuBarButton(const MenuBarButton&) = delete;

    /*! \brief Copy assignment is disabled because JUCE component ownership is not copyable. */
    MenuBarButton& operator=(const MenuBarButton&) = delete;

    /*! \brief Moving is disabled because child component registrations are not movable. */
    MenuBarButton(MenuBarButton&&) = delete;

    /*! \brief Move assignment is disabled because child component registrations are not movable. */
    MenuBarButton& operator=(MenuBarButton&&) = delete;

    /*!
    \brief Sets the displayed label.
    \param label_text New label text shown in the menu strip.
    */
    void setText(const juce::String& label_text);

    /*!
    \brief Returns the displayed label.
    \return Current label text.
    */
    [[nodiscard]] const juce::String& getText() const noexcept;

    /*!
    \brief Returns the preferred width for the current label at a menu-strip height.
    \param height Menu strip height used to match MenuBarComponent text sizing.
    \return Preferred width in pixels, including horizontal padding.
    */
    [[nodiscard]] int preferredWidthForHeight(int height) const;

private:
    // Paints the hover or pressed highlight and centered label text.
    void paintButton(
        juce::Graphics& g, bool should_draw_button_as_highlighted,
        bool should_draw_button_as_down) override;
};

} // namespace rock_hero::editor::ui
