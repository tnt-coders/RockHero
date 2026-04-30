/*!
\file timeline.h
\brief Song timeline time value types.
*/

#pragma once

#include <algorithm>
#include <compare>

namespace rock_hero::core
{

/*! \brief Position on the song timeline, measured in seconds from the start of the song. */
struct TimePosition
{
    /*! \brief Position value in seconds. */
    double seconds{0.0};

    /*! \brief Creates a zero-second timeline position. */
    constexpr TimePosition() noexcept = default;

    /*!
    \brief Creates a timeline position from seconds.
    \param seconds_value Position value in seconds.
    */
    explicit constexpr TimePosition(double seconds_value) noexcept
        : seconds{seconds_value}
    {}

    /*!
    \brief Compares two timeline positions by their second value.
    \param lhs Left-hand timeline position.
    \param rhs Right-hand timeline position.
    \return True when both positions store the same second value.
    */
    // Timeline value equality is intentionally exact. Tolerance-based comparisons should use
    // named timing helpers at the algorithm call site, not operator==.
    // This is not defaulted because the generated comparison uses direct floating-point ==,
    // which is promoted to a build error by -Wfloat-equal under the shared warning policy.
    // std::is_eq(lhs.seconds <=> rhs.seconds) preserves exact equality semantics while avoiding
    // that compiler diagnostic.
    friend constexpr bool operator==(const TimePosition& lhs, const TimePosition& rhs) noexcept
    {
        return std::is_eq(lhs.seconds <=> rhs.seconds);
    }
};

/*! \brief Duration on the song timeline, measured in seconds. */
struct TimeDuration
{
    /*! \brief Duration value in seconds. */
    double seconds{0.0};

    /*! \brief Creates a zero-second timeline duration. */
    constexpr TimeDuration() noexcept = default;

    /*!
    \brief Creates a timeline duration from seconds.
    \param seconds_value Duration value in seconds.
    */
    explicit constexpr TimeDuration(double seconds_value) noexcept
        : seconds{seconds_value}
    {}

    /*!
    \brief Compares two timeline durations by their second value.
    \param lhs Left-hand timeline duration.
    \param rhs Right-hand timeline duration.
    \return True when both durations store the same second value.
    */
    // Timeline value equality is intentionally exact. Tolerance-based comparisons should use
    // named timing helpers at the algorithm call site, not operator==.
    // This is not defaulted because the generated comparison uses direct floating-point ==,
    // which is promoted to a build error by -Wfloat-equal under the shared warning policy.
    // std::is_eq(lhs.seconds <=> rhs.seconds) preserves exact equality semantics while avoiding
    // that compiler diagnostic.
    friend constexpr bool operator==(const TimeDuration& lhs, const TimeDuration& rhs) noexcept
    {
        return std::is_eq(lhs.seconds <=> rhs.seconds);
    }
};

/*! \brief Project timeline range represented by start and end positions. */
struct TimeRange
{
    /*! \brief First position in the timeline range. */
    TimePosition start{};

    /*! \brief End position of the timeline range. */
    TimePosition end{};

    /*!
    \brief Calculates the non-negative duration between start and end.
    \return Duration from start to end, or zero when the range is inverted.
    */
    [[nodiscard]] constexpr TimeDuration duration() const noexcept
    {
        return TimeDuration{std::max(0.0, end.seconds - start.seconds)};
    }

    /*!
    \brief Reports whether a position lies inside the inclusive range endpoints.
    \param position Position to test.
    \return True when position is between start and end, including both endpoints.
    */
    [[nodiscard]] constexpr bool contains(TimePosition position) const noexcept
    {
        return position.seconds >= start.seconds && position.seconds <= end.seconds;
    }

    /*!
    \brief Clamps a position into the range.
    \param position Position to clamp.
    \return Clamped position, or start when the range has no positive duration.
    */
    [[nodiscard]] constexpr TimePosition clamp(TimePosition position) const noexcept
    {
        if (end.seconds <= start.seconds)
        {
            return start;
        }

        return TimePosition{std::clamp(position.seconds, start.seconds, end.seconds)};
    }

    /*!
    \brief Compares two time ranges by their endpoints.
    \param lhs Left-hand time range.
    \param rhs Right-hand time range.
    \return True when both ranges store equal start and end positions.
    */
    friend bool operator==(const TimeRange& lhs, const TimeRange& rhs) = default;
};

} // namespace rock_hero::core
