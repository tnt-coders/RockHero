/*!
\file difficulty.h
\brief Arrangement difficulty rating and derived tier helpers.
*/

#pragma once

#include <cstdint>
#include <type_traits>

namespace rock_hero::core
{

/*! \brief Display tier derived from an arrangement's numeric difficulty rating. */
enum class DifficultyTier : std::uint8_t
{
    /*! \brief Unrated, missing, or out-of-range difficulty. */
    Unknown,
    /*! \brief Difficulty tier for ratings 1 and 2. */
    Easy,
    /*! \brief Difficulty tier for ratings 3 and 4. */
    Medium,
    /*! \brief Difficulty tier for ratings 5 and 6. */
    Hard,
    /*! \brief Difficulty tier for ratings 7 and 8. */
    Expert,
    /*! \brief Difficulty tier for ratings 9 and 10. */
    Master
};

/*! \brief Numeric arrangement difficulty where zero means unknown and authored values are 1-10. */
struct DifficultyRating
{
    /*! \brief Numeric difficulty value; zero represents an unknown rating. */
    std::uint8_t value{0};

    /*! \brief Creates an unknown difficulty rating. */
    constexpr DifficultyRating() noexcept = default;

    /*!
    \brief Creates a difficulty rating from its numeric value.
    \param rating_value Numeric difficulty value.
    */
    explicit constexpr DifficultyRating(std::uint8_t rating_value) noexcept
        : value{rating_value}
    {}

    /*!
    \brief Compares two difficulty ratings by their numeric value.
    \param lhs Left-hand difficulty rating.
    \param rhs Right-hand difficulty rating.
    \return True when both ratings store the same numeric value.
    */
    friend bool operator==(const DifficultyRating& lhs, const DifficultyRating& rhs) = default;
};

static_assert(sizeof(DifficultyRating) <= 16);
static_assert(std::is_trivially_copyable_v<DifficultyRating>);

/*!
\brief Reports whether a difficulty rating is in the authored 1-10 range.
\param rating Difficulty rating to inspect.
\return True when the rating is between 1 and 10, inclusive.
*/
[[nodiscard]] constexpr bool isValid(DifficultyRating rating) noexcept
{
    return rating.value >= 1 && rating.value <= 10;
}

/*!
\brief Derives a display tier from a numeric difficulty rating.
\param rating Difficulty rating to classify.
\return Matching display tier, or DifficultyTier::Unknown for unknown or invalid ratings.
*/
[[nodiscard]] constexpr DifficultyTier difficultyTier(DifficultyRating rating) noexcept
{
    if (rating.value >= 1 && rating.value <= 2)
    {
        return DifficultyTier::Easy;
    }

    if (rating.value >= 3 && rating.value <= 4)
    {
        return DifficultyTier::Medium;
    }

    if (rating.value >= 5 && rating.value <= 6)
    {
        return DifficultyTier::Hard;
    }

    if (rating.value >= 7 && rating.value <= 8)
    {
        return DifficultyTier::Expert;
    }

    if (rating.value >= 9 && rating.value <= 10)
    {
        return DifficultyTier::Master;
    }

    return DifficultyTier::Unknown;
}

} // namespace rock_hero::core
