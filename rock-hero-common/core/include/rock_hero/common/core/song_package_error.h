/*!
\file song_package_error.h
\brief Typed errors reported by native Rock Hero song package persistence.
*/

#pragma once

#include <string>

namespace rock_hero::common::core
{

/*! \brief Stable native song package failure reasons. */
enum class SongPackageErrorCode
{
    /*! \brief The extracted package directory was missing or not a directory. */
    MissingPackageDirectory,

    /*! \brief The required native song document was missing. */
    MissingSongDocument,

    /*! \brief The native song document could not be opened. */
    CouldNotOpenSongDocument,

    /*! \brief The native song document was malformed or unsupported. */
    InvalidSongDocument,

    /*! \brief A native song audio asset was missing, unsafe, or could not be saved. */
    InvalidAudioAsset,

    /*! \brief A native song arrangement was missing, unsafe, or otherwise invalid. */
    InvalidArrangement,

    /*! \brief A native song package archive could not be extracted. */
    CouldNotExtractPackage,

    /*! \brief The output song directory could not be created. */
    CouldNotCreateSongDirectory,

    /*! \brief The native song document could not be written. */
    CouldNotWriteSongDocument,

    /*! \brief The native song package archive could not be written. */
    CouldNotWritePackage,
};

/*! \brief Native song package error with a stable code and diagnostic message. */
struct SongPackageError
{
    /*! \brief Stable reason for the song package failure. */
    SongPackageErrorCode code{};

    /*! \brief Human-readable diagnostic text for UI display or logs. */
    std::string message;

    /*!
    \brief Creates a song package error with the default message for the code.
    \param error_code Stable song package failure code.
    */
    explicit SongPackageError(SongPackageErrorCode error_code);

    /*!
    \brief Creates a song package error with explicit diagnostic text.
    \param error_code Stable song package failure code.
    \param message_text Human-readable diagnostic text.
    */
    SongPackageError(SongPackageErrorCode error_code, std::string message_text);

    /*!
    \brief Compares song package errors by stored code and message.
    \param lhs Left-hand song package error.
    \param rhs Right-hand song package error.
    \return True when both errors have equal fields.
    */
    friend bool operator==(const SongPackageError& lhs, const SongPackageError& rhs) = default;
};

} // namespace rock_hero::common::core
