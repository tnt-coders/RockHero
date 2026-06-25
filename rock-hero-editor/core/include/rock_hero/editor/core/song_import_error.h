/*!
\file song_import_error.h
\brief Typed errors reported by editor song importers.
*/

#pragma once

#include <string>

namespace rock_hero::editor::core
{

/*! \brief Stable editor song import failure reasons. */
enum class SongImportErrorCode
{
    /*! \brief The requested source file was missing. */
    MissingSource,

    /*! \brief Source archive extraction failed. */
    ExtractionFailed,

    /*! \brief Imported native song package data was missing or invalid. */
    InvalidImportedSong,
};

/*! \brief Editor song import error with a stable code and diagnostic message. */
struct [[nodiscard]] SongImportError
{
    /*! \brief Stable reason for the song import failure. */
    SongImportErrorCode code{};

    /*! \brief Human-readable diagnostic text for UI display or logs. */
    std::string message;

    /*!
    \brief Creates a song import error with the default message for the code.
    \param error_code Stable song import failure code.
    */
    explicit SongImportError(SongImportErrorCode error_code);

    /*!
    \brief Creates a song import error with explicit diagnostic text.
    \param error_code Stable song import failure code.
    \param message_text Human-readable diagnostic text.
    */
    SongImportError(SongImportErrorCode error_code, std::string message_text);
};

} // namespace rock_hero::editor::core
