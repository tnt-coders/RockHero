/*!
\file settings_file_options.h
\brief One storage policy shared by every Rock Hero properties file.
*/

#pragma once

#include <juce_data_structures/juce_data_structures.h>

namespace rock_hero::common::core
{

/*!
\brief Builds the properties-file options every Rock Hero settings file shares.

The options are the storage format only; where each file lives is user_data_folders.h's, and
every store opens its file at an explicit path. SharedSettingsFile additionally sets processLock,
because both products write that one file.

millisecondsBeforeSaving is zero deliberately, and is the one field a caller must never relax:
settings writes are acknowledged to the user, so they have to reach disk at the write rather than
waiting out JUCE's default three-second timer, where a crash would silently discard them.

Case-sensitive key names and XML storage are part of the on-disk contract, so they are assigned
explicitly below even though JUCE's defaults currently agree — a durable format guarantee must not
ride a framework default that could move. Saving stays enabled, JUCE's own default.

\return Properties-file options for a Rock Hero settings file.
*/
[[nodiscard]] inline juce::PropertiesFile::Options settingsFileOptions()
{
    juce::PropertiesFile::Options options;
    options.millisecondsBeforeSaving = 0;
    options.ignoreCaseOfKeyNames = false;
    options.storageFormat = juce::PropertiesFile::storeAsXML;
    return options;
}

} // namespace rock_hero::common::core
