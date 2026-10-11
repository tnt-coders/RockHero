/*!
\file user_data_folders.h
\brief The one authority for every per-user location both products read and write.
*/

#pragma once

#include <filesystem>
#include <rock_hero/common/core/shared/application_identity.h>

namespace rock_hero::common::core
{

/*!
\brief Returns one application's own settings file, `settings/<token>.settings`.
\param product The application whose file it is.
\return The file's path in the per-user folder; the file may not exist yet.
*/
[[nodiscard]] std::filesystem::path settingsFile(Product product);

/*!
\brief Returns the settings file both applications share and lock while writing.
\return `settings/shared.settings` in the per-user folder; the file may not exist yet.
*/
[[nodiscard]] std::filesystem::path sharedSettingsFile();

/*!
\brief Returns one application's rolling log file, `logs/<token>.log`.
\param product The application whose log it is.
\return The log's path in the per-user folder; the logger creates the folder.
*/
[[nodiscard]] std::filesystem::path logFile(Product product);

/*!
\brief Returns the folder the player's own highway themes are loaded from.
\return `themes/` in the per-user folder; it may not exist.
*/
[[nodiscard]] std::filesystem::path themesFolder();

/*!
\brief Returns the default song folder the game's library always scans first.
\return `songs/` in the per-user folder; the game creates it on demand.
*/
[[nodiscard]] std::filesystem::path songsFolder();

/*!
\brief Returns the folder Tracktion keeps its own settings and plugin-scan state in.
\return `tracktion/` in the per-user folder; it may not exist yet.
*/
[[nodiscard]] std::filesystem::path tracktionFolder();

/*!
\brief Returns the root of every scratch directory: session and project workspaces, Tracktion's
temporary files.

Scratch is never user data, so it sits under the platform temporary directory, not beside the
settings.

\return The per-user scratch root; it may not exist yet.
*/
[[nodiscard]] std::filesystem::path scratchFolder();

} // namespace rock_hero::common::core
