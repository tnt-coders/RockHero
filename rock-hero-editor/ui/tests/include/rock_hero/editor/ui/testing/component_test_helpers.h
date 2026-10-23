/*!
\file component_test_helpers.h
\brief Shared JUCE component helpers for editor UI tests.
*/

#pragma once

#include <algorithm>
#include <initializer_list>
#include <juce_gui_basics/juce_gui_basics.h>
#include <stdexcept>
#include <string>
#include <vector>

namespace rock_hero::editor::ui::testing
{

/*!
\brief Searches a component tree by component ID.

The ID parameter is a by-value juce::StringRef on purpose: GCC's -Wdangling-reference heuristic
flags any reference bound to a reference-returning call that also received a temporary through a
reference parameter, which is exactly what literal IDs passed as const juce::String& would be.
A value parameter gives the heuristic nothing to key on, and matches JUCE's own ID-lookup API.

\param parent Root component to search, including the parent itself.
\param id Component ID to find.
\return Matching component, or nullptr when no descendant has the requested ID.
*/
[[nodiscard]] inline juce::Component* findDescendant(juce::Component& parent, juce::StringRef id)
{
    if (parent.getComponentID() == id)
    {
        return &parent;
    }

    // The pointee stays non-const because the pointer escapes through the non-const return.
    // clang-tidy 21's misc-const-correctness misses that escape and demands a pointee-const that
    // would not compile; clang-tidy 22, which CI and local tooling pin, does not.
    juce::Component* matched_child = nullptr;
    for (int index = 0; index < parent.getNumChildComponents() && matched_child == nullptr; ++index)
    {
        juce::Component* const child = parent.getChildComponent(index);
        if (child != nullptr)
        {
            matched_child = findDescendant(*child, id);
        }
    }

    return matched_child;
}

/*!
\brief Returns a required descendant component by component ID and concrete type.
\param parent Root component to search, including the parent itself.
\param id Component ID to find.
\return Matching component cast to ComponentType.
\throws std::runtime_error when the component is missing or has a different type.
*/
template <class ComponentType>
[[nodiscard]] ComponentType& findRequiredDescendant(juce::Component& parent, juce::StringRef id)
{
    juce::Component* const child = findDescendant(parent, id);
    if (child == nullptr)
    {
        throw std::runtime_error{"Missing descendant component: " + juce::String{id}.toStdString()};
    }

    auto* const typed_child = dynamic_cast<ComponentType*>(child);
    if (typed_child == nullptr)
    {
        throw std::runtime_error{
            "Descendant component has unexpected type: " + juce::String{id}.toStdString()
        };
    }

    return *typed_child;
}

/*!
\brief Returns a required direct child component by component ID and concrete type.
\param parent Parent component whose immediate children should be searched.
\param id Component ID to find.
\return Matching direct child cast to ComponentType.
\throws std::runtime_error when the component is missing or has a different type.
*/
template <class ComponentType>
[[nodiscard]] ComponentType& findRequiredDirectChild(juce::Component& parent, juce::StringRef id)
{
    juce::Component* const child = parent.findChildWithID(id);
    if (child == nullptr)
    {
        throw std::runtime_error{
            "Missing direct child component: " + juce::String{id}.toStdString()
        };
    }

    auto* const typed_child = dynamic_cast<ComponentType*>(child);
    if (typed_child == nullptr)
    {
        throw std::runtime_error{
            "Direct child component has unexpected type: " + juce::String{id}.toStdString()
        };
    }

    return *typed_child;
}

/*!
\brief Creates a left-button mouse event positioned relative to the supplied component.
\param component Component that should receive the event.
\param x Horizontal local position.
\param y Vertical local position.
\param modifiers Mouse button and keyboard modifiers active for the event.
\return Mouse event suitable for direct mouseDown() or mouseUp() dispatch in tests.
*/
[[nodiscard]] inline juce::MouseEvent makeMouseDownEvent(
    juce::Component& component, float x, float y,
    juce::ModifierKeys modifiers = juce::ModifierKeys::leftButtonModifier)
{
    const juce::Point<float> position{x, y};
    const juce::Time event_time = juce::Time::getCurrentTime();

    return {
        juce::Desktop::getInstance().getMainMouseSource(),
        position,
        modifiers,
        1.0f,
        0.0f,
        0.0f,
        0.0f,
        0.0f,
        &component,
        &component,
        event_time,
        position,
        event_time,
        1,
        false
    };
}

/*!
\brief Creates a mouse-drag event with a distinct press origin.

Unlike makeMouseDownEvent, the event reports a real drag distance from \p down_x / \p down_y and
answers true to mouseWasDraggedSinceMouseDown(), so gestures guarded by the click/drag threshold
(which ignores the micro-jiggle inside a click) treat it as a genuine drag.

\param component Component that should receive the event.
\param x Horizontal local position of the drag.
\param y Vertical local position of the drag.
\param down_x Horizontal local position of the original press.
\param down_y Vertical local position of the original press.
\param modifiers Mouse button and keyboard modifiers active for the event.
\return Mouse event suitable for direct mouseDrag() dispatch in tests.
*/
[[nodiscard]] inline juce::MouseEvent makeMouseDragEvent(
    juce::Component& component, float x, float y, float down_x, float down_y,
    juce::ModifierKeys modifiers = juce::ModifierKeys::leftButtonModifier)
{
    const juce::Time event_time = juce::Time::getCurrentTime();

    return {
        juce::Desktop::getInstance().getMainMouseSource(),
        juce::Point<float>{x, y},
        modifiers,
        1.0f,
        0.0f,
        0.0f,
        0.0f,
        0.0f,
        &component,
        &component,
        event_time,
        juce::Point<float>{down_x, down_y},
        event_time,
        1,
        true
    };
}

/*!
\brief Presses and releases a JUCE button through its normal mouse path.
\param button Button to click.
*/
inline void clickButton(juce::Button& button)
{
    const juce::MouseEvent event = makeMouseDownEvent(button, 5.0f, 5.0f);
    auto& component = static_cast<juce::Component&>(button);
    component.mouseDown(event);
    component.mouseUp(event);
}

/*!
\brief Returns the squared distance between two colors' RGB, the one measure a pixel's color class
is decided by.
\param lhs One color.
\param rhs The other.
\return The sum of the squared channel differences.
*/
[[nodiscard]] inline int squaredColorDistance(const juce::Colour lhs, const juce::Colour rhs)
{
    const int red = lhs.getRed() - rhs.getRed();
    const int green = lhs.getGreen() - rhs.getGreen();
    const int blue = lhs.getBlue() - rhs.getBlue();
    return (red * red) + (green * green) + (blue * blue);
}

/*!
\brief Classifies a pixel as the nearest of the colors a probe knows can stand there, so the
probe holds whatever antialiasing level a platform's rasterizer lands on.
\param pixel The pixel read back.
\param known The colors the pixel can be.
\return The nearest of \p known, the first on a tie.
*/
[[nodiscard]] inline juce::Colour nearestColor(
    const juce::Colour pixel, const std::initializer_list<juce::Colour> known)
{
    return std::ranges::min(known, {}, [pixel](const juce::Colour candidate) {
        return squaredColorDistance(pixel, candidate);
    });
}

/*!
\brief Names every button and combo box under a component that a mouse click would hand keyboard
focus.

The editor's focus ring is a keyboard signal, so every focusable button and combo box is built with
`setMouseClickGrabsKeyboardFocus(false)`. A control that never takes focus (a scroll bar's arrow
buttons, a mode toggle) cannot take it by a click either, so it is not reported.

\param root Component whose subtree is walked, itself included.
\return The component id (or, without one, the name) of each offending control; empty when none.
*/
[[nodiscard]] inline std::vector<std::string> clickFocusedControls(juce::Component& root)
{
    std::vector<std::string> offenders;
    const auto visit = [&offenders](const auto& self, juce::Component& component) -> void {
        const bool control = dynamic_cast<juce::Button*>(&component) != nullptr ||
                             dynamic_cast<juce::ComboBox*>(&component) != nullptr;
        if (control && component.getWantsKeyboardFocus() &&
            component.getMouseClickGrabsKeyboardFocus())
        {
            const juce::String label = component.getComponentID().isNotEmpty()
                                           ? component.getComponentID()
                                           : component.getName();
            offenders.push_back(label.toStdString());
        }
        for (juce::Component* const child : component.getChildren())
        {
            self(self, *child);
        }
    };
    visit(visit, root);
    return offenders;
}

} // namespace rock_hero::editor::ui::testing
