/*!
\file timeline.h
\brief Song timeline time value types.
*/

#pragma once

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
    friend bool operator==(const TimePosition& lhs, const TimePosition& rhs) = default;
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
    friend bool operator==(const TimeDuration& lhs, const TimeDuration& rhs) = default;
};

} // namespace rock_hero::core
