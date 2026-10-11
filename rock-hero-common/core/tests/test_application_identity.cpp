#include <catch2/catch_test_macros.hpp>
#include <rock_hero/common/core/shared/application_identity.h>
#include <string_view>

namespace rock_hero::common::core
{

// Pins the persisted names: every per-user file is found by these strings, so a change here moves
// every user's data.
TEST_CASE(
    "Application identity names the data folder and each product's files", "[core][application]")
{
    CHECK(productName() == std::string_view{"Rock Hero"});
    CHECK(applicationDataFolderName() == std::string_view{"Rock Hero"});
    CHECK(productToken(Product::Game) == std::string_view{"game"});
    CHECK(productToken(Product::Editor) == std::string_view{"editor"});
}

} // namespace rock_hero::common::core
