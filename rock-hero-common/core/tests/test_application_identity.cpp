#include <catch2/catch_test_macros.hpp>
#include <rock_hero/common/core/shared/application_identity.h>
#include <string_view>

namespace rock_hero::common::core
{

// Pins the persisted names: settings, logs and Tracktion data are found by these strings, so a
// change here moves every user's data folder.
TEST_CASE("Application identity names the canonical app data folder", "[core][application]")
{
    CHECK(productName() == std::string_view{"Rock Hero"});
    CHECK(applicationDataFolderName() == std::string_view{"Rock Hero"});
    CHECK(editorApplicationName() == std::string_view{"Rock Hero Editor"});
}

} // namespace rock_hero::common::core
