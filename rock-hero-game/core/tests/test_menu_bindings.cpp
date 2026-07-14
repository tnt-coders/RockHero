#include <catch2/catch_test_macros.hpp>
#include <optional>
#include <rock_hero/game/core/input/menu_action.h>
#include <rock_hero/game/core/input/menu_bindings.h>
#include <rock_hero/game/core/input/menu_input_trigger.h>
#include <vector>

namespace rock_hero::game::core
{

namespace
{

// A keyboard trigger with the given code.
[[nodiscard]] MenuInputTrigger key(const int code)
{
    return MenuInputTrigger{.source = MenuInputSource::Keyboard, .code = code};
}

// A gamepad trigger with the given code.
[[nodiscard]] MenuInputTrigger pad(const int code)
{
    return MenuInputTrigger{.source = MenuInputSource::Gamepad, .code = code};
}

} // namespace

// Verifies a bound trigger resolves to its action and an unbound one resolves to nothing.
TEST_CASE("Menu bindings resolve bound triggers only", "[core][input]")
{
    MenuBindings bindings;
    bindings.bind(MenuAction::Accept, key(13));

    CHECK(bindings.resolve(key(13)) == std::optional{MenuAction::Accept});
    CHECK_FALSE(bindings.resolve(key(27)).has_value());
    // Same code, different device is a different trigger.
    CHECK_FALSE(bindings.resolve(pad(13)).has_value());
}

// Verifies several triggers (across devices) can drive one action.
TEST_CASE("Menu bindings map several triggers to one action", "[core][input]")
{
    MenuBindings bindings;
    bindings.bind(MenuAction::NavigateUp, key(82)); // keyboard up
    bindings.bind(MenuAction::NavigateUp, pad(11)); // gamepad dpad-up

    CHECK(bindings.resolve(key(82)) == std::optional{MenuAction::NavigateUp});
    CHECK(bindings.resolve(pad(11)) == std::optional{MenuAction::NavigateUp});

    const std::vector<MenuInputTrigger> triggers = bindings.triggersFor(MenuAction::NavigateUp);
    REQUIRE(triggers.size() == 2);
    // Keyboard sorts before Gamepad, so the keyboard trigger comes first.
    CHECK(triggers.front() == key(82));
    CHECK(triggers.back() == pad(11));
}

// Verifies rebinding a trigger moves it off its old action (overwrite-and-clear).
TEST_CASE("Menu bindings rebind overwrites and clears the old action", "[core][input]")
{
    MenuBindings bindings;
    bindings.bind(MenuAction::Accept, key(13));
    REQUIRE(bindings.resolve(key(13)) == std::optional{MenuAction::Accept});

    // Rebind the same trigger to Back.
    bindings.bind(MenuAction::Back, key(13));

    CHECK(bindings.resolve(key(13)) == std::optional{MenuAction::Back});
    // The trigger no longer belongs to Accept.
    CHECK(bindings.triggersFor(MenuAction::Accept).empty());
    REQUIRE(bindings.triggersFor(MenuAction::Back).size() == 1);
    CHECK(bindings.triggersFor(MenuAction::Back).front() == key(13));
}

// Verifies unbinding removes only that trigger and leaves the rest intact.
TEST_CASE("Menu bindings unbind removes a single trigger", "[core][input]")
{
    MenuBindings bindings;
    bindings.bind(MenuAction::NavigateDown, key(81));
    bindings.bind(MenuAction::NavigateDown, pad(12));

    bindings.unbind(key(81));

    CHECK_FALSE(bindings.resolve(key(81)).has_value());
    CHECK(bindings.resolve(pad(12)) == std::optional{MenuAction::NavigateDown});
    REQUIRE(bindings.triggersFor(MenuAction::NavigateDown).size() == 1);
    CHECK(bindings.triggersFor(MenuAction::NavigateDown).front() == pad(12));
}

// Verifies an action with no bound triggers reports an empty list.
TEST_CASE("Menu bindings report no triggers for an unbound action", "[core][input]")
{
    MenuBindings bindings;
    bindings.bind(MenuAction::Accept, key(13));

    CHECK(bindings.triggersFor(MenuAction::Rescan).empty());
}

} // namespace rock_hero::game::core
