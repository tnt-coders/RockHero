#include <catch2/catch_test_macros.hpp>
#include <rock_hero/core/difficulty.h>

namespace rock_hero::core
{

// Verifies the default difficulty rating explicitly represents an unrated arrangement.
TEST_CASE("DifficultyRating defaults to unknown", "[core][difficulty]")
{
    constexpr DifficultyRating rating;

    CHECK(rating.value == 0);
    CHECK_FALSE(isValid(rating));
    CHECK(difficultyTier(rating) == DifficultyTier::Unknown);
}

// Verifies only authored ratings from one through ten are valid.
TEST_CASE("DifficultyRating validates authored range", "[core][difficulty]")
{
    CHECK_FALSE(isValid(DifficultyRating{}));
    CHECK(isValid(DifficultyRating{1}));
    CHECK(isValid(DifficultyRating{10}));
    CHECK_FALSE(isValid(DifficultyRating{11}));
}

// Verifies the numeric rating scale maps into two-value display tiers.
TEST_CASE("DifficultyRating maps values to tiers", "[core][difficulty]")
{
    CHECK(difficultyTier(DifficultyRating{}) == DifficultyTier::Unknown);
    CHECK(difficultyTier(DifficultyRating{1}) == DifficultyTier::Easy);
    CHECK(difficultyTier(DifficultyRating{2}) == DifficultyTier::Easy);
    CHECK(difficultyTier(DifficultyRating{3}) == DifficultyTier::Medium);
    CHECK(difficultyTier(DifficultyRating{4}) == DifficultyTier::Medium);
    CHECK(difficultyTier(DifficultyRating{5}) == DifficultyTier::Hard);
    CHECK(difficultyTier(DifficultyRating{6}) == DifficultyTier::Hard);
    CHECK(difficultyTier(DifficultyRating{7}) == DifficultyTier::Expert);
    CHECK(difficultyTier(DifficultyRating{8}) == DifficultyTier::Expert);
    CHECK(difficultyTier(DifficultyRating{9}) == DifficultyTier::Master);
    CHECK(difficultyTier(DifficultyRating{10}) == DifficultyTier::Master);
    CHECK(difficultyTier(DifficultyRating{11}) == DifficultyTier::Unknown);
}

} // namespace rock_hero::core
