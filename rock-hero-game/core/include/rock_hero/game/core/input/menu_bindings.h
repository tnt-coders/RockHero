/*!
\file menu_bindings.h
\brief Maps raw input triggers to menu actions, with rebinding and conflict handling.
*/

#pragma once

#include <map>
#include <optional>
#include <rock_hero/game/core/input/menu_action.h>
#include <rock_hero/game/core/input/menu_input_trigger.h>
#include <vector>

namespace rock_hero::game::core
{

/*!
\brief Resolves raw input triggers to menu actions.

Headless and device-agnostic: the composition installs the concrete triggers (an adapter maps SDL
keycodes, gamepad buttons, or MIDI notes into MenuInputTrigger), and this class only decides which
action a trigger produces. Each trigger maps to at most one action, so binding a trigger already
used elsewhere moves it (overwrite-and-clear); an action may hold several triggers, so keyboard and
gamepad can both drive it. Starts empty — device defaults are the adapter's to install, which keeps
game/core free of any windowing dependency.
*/
class MenuBindings
{
public:
    /*!
    \brief Resolves a trigger to its bound action.
    \param trigger Raw input trigger to look up.
    \return The bound action, or std::nullopt when the trigger is unbound.
    */
    [[nodiscard]] std::optional<MenuAction> resolve(const MenuInputTrigger& trigger) const;

    /*!
    \brief Binds a trigger to an action, moving it off any other action it held.
    \param action Action the trigger should produce.
    \param trigger Raw input trigger to bind.
    */
    void bind(MenuAction action, const MenuInputTrigger& trigger);

    /*!
    \brief Removes a trigger's binding, if it has one.
    \param trigger Raw input trigger to unbind.
    */
    void unbind(const MenuInputTrigger& trigger);

    /*!
    \brief The triggers currently bound to an action.
    \param action Action to list triggers for.
    \return The bound triggers in trigger order; empty when the action has none.
    */
    [[nodiscard]] std::vector<MenuInputTrigger> triggersFor(MenuAction action) const;

private:
    // A trigger maps to at most one action; binding a trigger overwrites its prior action, which is
    // exactly the overwrite-and-clear conflict rule. An action may key several triggers.
    std::map<MenuInputTrigger, MenuAction> m_action_by_trigger;
};

} // namespace rock_hero::game::core
