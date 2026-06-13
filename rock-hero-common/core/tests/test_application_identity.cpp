#include <catch2/catch_test_macros.hpp>
#include <rock_hero/common/core/application_identity.h>
#include <string_view>

namespace rock_hero::common::core
{

TEST_CASE("Application identity names the canonical app data folder", "[core][application]")
{
    CHECK(productName() == std::string_view{"Rock Hero"});
    CHECK(applicationDataFolderName() == std::string_view{"Rock Hero"});
    CHECK(editorApplicationName() == std::string_view{"Rock Hero Editor"});
}

} // namespace rock_hero::common::core
