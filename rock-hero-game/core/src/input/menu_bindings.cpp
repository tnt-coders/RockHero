#include "input/menu_bindings.h"

namespace rock_hero::game::core
{

std::optional<MenuAction> MenuBindings::resolve(MenuInputTrigger trigger) const
{
    if (const auto found = m_action_by_trigger.find(trigger); found != m_action_by_trigger.end())
    {
        return found->second;
    }
    return std::nullopt;
}

void MenuBindings::bind(MenuAction action, MenuInputTrigger trigger)
{
    // Assigning overwrites any prior action for this trigger, moving it off the old action.
    m_action_by_trigger[trigger] = action;
}

void MenuBindings::unbind(MenuInputTrigger trigger)
{
    m_action_by_trigger.erase(trigger);
}

std::vector<MenuInputTrigger> MenuBindings::triggersFor(MenuAction action) const
{
    // Map iteration is ordered by trigger, so the returned triggers are already in trigger order.
    std::vector<MenuInputTrigger> triggers;
    for (const auto& [trigger, bound_action] : m_action_by_trigger)
    {
        if (bound_action == action)
        {
            triggers.push_back(trigger);
        }
    }
    return triggers;
}

} // namespace rock_hero::game::core
