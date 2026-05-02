/*!
\file track.h
\brief Role-free track data used by editable sessions.
*/

#pragma once

#include <cstdint>
#include <optional>
#include <rock_hero/core/audio_asset.h>
#include <rock_hero/core/timeline.h>
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

/*! \brief Stable identity for an audio clip within one Session. */
struct AudioClipId
{
    /*! \brief Numeric id value; zero is reserved as invalid. */
    std::uint64_t value{0};

    /*! \brief Creates an invalid audio clip id. */
    constexpr AudioClipId() noexcept = default;

    /*!
    \brief Creates an audio clip id from a numeric value.
    \param id_value Numeric id value.
    */
    explicit constexpr AudioClipId(std::uint64_t id_value) noexcept
        : value{id_value}
    {}

    /*!
    \brief Reports whether the id can refer to a session audio clip.
    \return True when the id value is nonzero.
    */
    [[nodiscard]] constexpr bool isValid() const noexcept
    {
        return value != 0;
    }

    /*!
    \brief Compares two audio clip ids by their numeric value.
    \param lhs Left-hand audio clip id.
    \param rhs Right-hand audio clip id.
    \return True when both ids store the same numeric value.
    */
    friend bool operator==(const AudioClipId& lhs, const AudioClipId& rhs) = default;
};

/*! \brief One placed audio region on a track. */
struct AudioClip
{
    /*! \brief Stable id assigned by the owning Session; zero before Session stores the clip. */
    AudioClipId id;

    /*! \brief Audio asset referenced by the clip. */
    AudioAsset asset;

    /*! \brief Full natural duration of the referenced asset. */
    TimeDuration asset_duration;

    /*! \brief Range inside the asset that this clip plays. */
    TimeRange source_range;

    /*! \brief Start position of the clip on the session timeline. */
    TimePosition position;

    /*!
    \brief Calculates the range occupied by this clip on the session timeline.
    \return Timeline range from position through the source range duration.
    */
    [[nodiscard]] constexpr TimeRange timelineRange() const noexcept
    {
        return TimeRange{
            .start = position,
            .end = TimePosition{position.seconds + source_range.duration().seconds},
        };
    }

    /*!
    \brief Compares two audio clips by id, asset, source range, and timeline placement.
    \param lhs Left-hand clip.
    \param rhs Right-hand clip.
    \return True when both clips store equal values.
    */
    friend bool operator==(const AudioClip& lhs, const AudioClip& rhs) = default;
};

/*! \brief Role-free audio track stored by a Session. */
struct Track
{
    /*! \brief Stable id assigned by the owning Session. */
    TrackId id;

    /*! \brief User-visible track name. */
    std::string name;

    /*! \brief Optional audio clip currently assigned to this track. */
    // TODO: Change to std::vector when tracks can support multiple audio clips
    std::optional<AudioClip> audio_clip;
};

} // namespace rock_hero::core
