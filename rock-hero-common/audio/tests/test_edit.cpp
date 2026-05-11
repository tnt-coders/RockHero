#include <catch2/catch_test_macros.hpp>
#include <rock_hero/common/audio/i_edit.h>
#include <type_traits>

namespace rock_hero::common::audio
{

namespace
{

// Minimal placeholder implementation used until real edit commands are added.
class FakeEdit final : public IEdit
{
};

} // namespace

// Verifies IEdit remains reserved for future undoable edit commands.
TEST_CASE("IEdit is an empty edit-command placeholder", "[audio][edit]")
{
    CHECK(std::is_polymorphic_v<FakeEdit>);
}

} // namespace rock_hero::common::audio
