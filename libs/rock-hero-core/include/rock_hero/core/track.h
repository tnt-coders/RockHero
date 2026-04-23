/*!
\file track.h
\brief Role-free track data used by editable sessions.
*/

#pragma once

#include <cstdint>
#include <optional>
#include <rock_hero/core/audio_asset.h>
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

/*! \brief Role-free audio track stored by a Session. */
struct Track
{
    /*! \brief Stable id assigned by the owning Session. */
    TrackId id;

    /*! \brief User-visible track name. */
    std::string name;

    /*! \brief Optional audio asset currently assigned to this track. */
    std::optional<AudioAsset> audio_asset;
};

} // namespace rock_hero::core
