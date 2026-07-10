/*!
\file component_test_helpers.h
\brief Shared JUCE component helpers for editor UI tests.
*/

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <stdexcept>
#include <string>

namespace rock_hero::editor::ui::testing
{

/*!
\brief Searches a component tree by component ID.
\param parent Root component to search, including the parent itself.
\param id Component ID to find.
\return Matching component, or nullptr when no descendant has the requested ID.
*/
[[nodiscard]] inline juce::Component* findDescendant(
    juce::Component& parent, const juce::String& id)
{
    if (parent.getComponentID() == id)
    {
        return &parent;
    }

    // clang-tidy 21's misc-const-correctness pointee analysis cannot see that this pointer
    // escapes through the non-const return and demands a pointee-const that would not compile;
    // the false positive is fixed in clang-tidy 22, which CI and local tooling pin.
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
[[nodiscard]] ComponentType& findRequiredDescendant(juce::Component& parent, const juce::String& id)
{
    juce::Component* const child = findDescendant(parent, id);
    if (child == nullptr)
    {
        throw std::runtime_error{"Missing descendant component: " + id.toStdString()};
    }

    auto* const typed_child = dynamic_cast<ComponentType*>(child);
    if (typed_child == nullptr)
    {
        throw std::runtime_error{"Descendant component has unexpected type: " + id.toStdString()};
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
[[nodiscard]] ComponentType& findRequiredDirectChild(
    juce::Component& parent, const juce::String& id)
{
    juce::Component* const child = parent.findChildWithID(id);
    if (child == nullptr)
    {
        throw std::runtime_error{"Missing direct child component: " + id.toStdString()};
    }

    auto* const typed_child = dynamic_cast<ComponentType*>(child);
    if (typed_child == nullptr)
    {
        throw std::runtime_error{"Direct child component has unexpected type: " + id.toStdString()};
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

} // namespace rock_hero::editor::ui::testing
