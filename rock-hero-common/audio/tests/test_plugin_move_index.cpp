#include "tracktion/plugin_move_index.h"

#include <catch2/catch_test_macros.hpp>

namespace rock_hero::common::audio
{

// Verifies down moves compensate for Tracktion removing the source after choosing a sibling.
TEST_CASE("Plugin move index shifts downward moves", "[audio][plugin-host]")
{
    CHECK(tracktionInsertionIndexForExistingPluginMove(2, 4) == 3);
    CHECK(tracktionInsertionIndexForExistingPluginMove(3, 5) == 4);
}

// Verifies up moves, appends, and unknown source indices keep Tracktion's requested index.
TEST_CASE("Plugin move index preserves stable move slots", "[audio][plugin-host]")
{
    CHECK(tracktionInsertionIndexForExistingPluginMove(4, 2) == 2);
    CHECK(tracktionInsertionIndexForExistingPluginMove(2, -1) == -1);
    CHECK(tracktionInsertionIndexForExistingPluginMove(-1, 4) == 4);
}

} // namespace rock_hero::common::audio
