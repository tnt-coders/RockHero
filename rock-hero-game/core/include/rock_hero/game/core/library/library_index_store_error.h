/*!
\file library_index_store_error.h
\brief Typed errors returned by library index persistence.
*/

#pragma once

#include <cstdint>
#include <string>

namespace rock_hero::game::core
{

/*! \brief Stable failure reasons for library index persistence operations. */
enum class LibraryIndexStoreErrorCode : std::uint8_t
{
    /*! \brief The index file could not be written. */
    CouldNotWriteIndex,
};

/*! \brief Recoverable library index persistence failure with a stable code and detail. */
struct [[nodiscard]] LibraryIndexStoreError
{
    /*! \brief Stable error code used by callers for branching. */
    LibraryIndexStoreErrorCode code{};

    /*! \brief Human-readable diagnostic suitable for UI display or logs. */
    std::string message;

    /*!
    \brief Creates an error with the default message for its code.
    \param error_code Stable error code used by callers for branching.
    */
    explicit LibraryIndexStoreError(LibraryIndexStoreErrorCode error_code);

    /*!
    \brief Creates an error with contextual diagnostic text.
    \param error_code Stable error code used by callers for branching.
    \param message_text Human-readable diagnostic suitable for UI display or logs.
    */
    LibraryIndexStoreError(LibraryIndexStoreErrorCode error_code, std::string message_text);
};

} // namespace rock_hero::game::core
