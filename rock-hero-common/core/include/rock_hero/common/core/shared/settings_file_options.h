/*!
\file settings_file_options.h
\brief One settings-file location policy shared by every Rock Hero properties file.
*/

#pragma once

#include <juce_data_structures/juce_data_structures.h>
#include <rock_hero/common/core/shared/application_identity.h>
#include <string_view>

namespace rock_hero::common::core
{

/*!
\brief Builds the properties-file options every Rock Hero settings file shares.

Editor workflow state, game profile state, and each app's audio config are separate files that
deliberately sit side by side in one per-user folder under one naming scheme, so that policy is
stated here once rather than restated per store. Callers vary only the application name, which
names the file inside the folder, plus doNotSave when the store is a read-only view of a file
another application owns.

millisecondsBeforeSaving is zero deliberately, and is the one field a caller must never relax:
settings writes are acknowledged to the user, so they have to reach disk at the write rather than
waiting out JUCE's default three-second timer, where a crash would silently discard them.

Case-sensitive key names and XML storage are part of the on-disk contract, so they are assigned
explicitly below even though JUCE's defaults currently agree — a durable format guarantee must not
ride a framework default that could move. The remaining fields are left at JUCE's own defaults,
which match this project's policy without being contractual: per-user rather than all-users
(commonToAllUsers), saving enabled, and no interprocess lock, because every settings file has
exactly one writing application.

\param application_name Application name that names the file inside the shared folder.
\return Properties-file options for that application's settings file.
*/
[[nodiscard]] inline juce::PropertiesFile::Options settingsFileOptions(
    std::string_view application_name)
{
    const std::string_view folder_name = applicationDataFolderName();

    juce::PropertiesFile::Options options;
    options.applicationName = juce::String{application_name.data(), application_name.size()};
    options.filenameSuffix = ".settings";
    options.folderName = juce::String{folder_name.data(), folder_name.size()};
    options.osxLibrarySubFolder = "Application Support";
    options.millisecondsBeforeSaving = 0;
    options.ignoreCaseOfKeyNames = false;
    options.storageFormat = juce::PropertiesFile::storeAsXML;
    return options;
}

} // namespace rock_hero::common::core
