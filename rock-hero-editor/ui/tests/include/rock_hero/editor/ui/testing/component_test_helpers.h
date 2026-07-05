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

    for (int index = 0; index < parent.getNumChildComponents(); ++index)
    {
        juce::Component* const child = parent.getChildComponent(index);
        if (child == nullptr)
        {
            continue;
        }

        if (juce::Component* const matched_child = findDescendant(*child, id);
            matched_child != nullptr)
        {
            return matched_child;
        }
    }

    return nullptr;
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
