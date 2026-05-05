/*!
\file track.h
\brief Role-free track value types used by editable sessions.
*/

#pragma once

#include <cstdint>
#include <optional>
#include <rock_hero/core/audio_clip.h>
#include <string>

namespace rock_hero::core
{

/*! \brief Stable identity for a track within one Session. */
struct TrackId
{
    /*! \brief Numeric id value; zero is reserved as invalid. */
    std::uint64_t value{0};

    /*! \brief Creates an invalid track id. */
    constexpr TrackId() noexcept = default;

    /*!
    \brief Creates a track id from a numeric value.
    \param id_value Numeric id value.
    */
    explicit constexpr TrackId(std::uint64_t id_value) noexcept
        : value{id_value}
    {}

    /*!
    \brief Reports whether the id can refer to a session track.
    \return True when the id value is nonzero.
    */
    [[nodiscard]] constexpr bool isValid() const noexcept
    {
        return value != 0;
    }

    /*!
    \brief Compares two track ids by their numeric value.
    \param lhs Left-hand track id.
    \param rhs Right-hand track id.
    \return True when both ids store the same numeric value.
    */
    friend bool operator==(const TrackId& lhs, const TrackId& rhs) = default;
};

/*! \brief Identity-free track specification before Session identity is attached. */
struct TrackSpec
{
    /*! \brief User-visible track name. */
    std::string name;

    /*!
    \brief Compares two track specs by their stored fields.
    \param lhs Left-hand track spec.
    \param rhs Right-hand track spec.
    \return True when both track specs store equal values.
    */
    friend bool operator==(const TrackSpec& lhs, const TrackSpec& rhs) = default;
};

/*! \brief Role-free audio track stored by a Session. */
struct Track
{
    /*! \brief Stable id allocated by the owning Session. */
    TrackId id;

    /*! \brief User-visible track name. */
    std::string name;

    /*! \brief Optional audio clip currently assigned to this track. */
    // TODO: Change to std::vector when tracks can support multiple audio clips
    std::optional<AudioClip> audio_clip;
};

} // namespace rock_hero::core
