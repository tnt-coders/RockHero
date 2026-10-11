#include "shared/shared_settings_file.h"

#include <rock_hero/common/core/shared/juce_path.h>
#include <rock_hero/common/core/shared/settings_file_options.h>

namespace rock_hero::common::core
{

namespace
{

// The lock is named by the file it guards, so two products opening one file share one lock. The
// path is hashed because JUCE turns a lock name into a file name on macOS and Linux, where a path's
// separators would not survive.
[[nodiscard]] juce::String lockNameFor(const juce::File& file)
{
    return "RockHeroSettings-" + juce::String::toHexString(file.getFullPathName().hashCode64());
}

// The shared storage policy, with the file's process lock set to the guarding lock.
[[nodiscard]] juce::PropertiesFile::Options lockedOptions(juce::InterProcessLock& lock)
{
    juce::PropertiesFile::Options options = settingsFileOptions();
    options.processLock = &lock;
    return options;
}

} // namespace

SharedSettingsFile::SharedSettingsFile(const std::filesystem::path& file)
    : m_file(juceFileFromPath(file))
    , m_lock(lockNameFor(m_file))
    , m_options(lockedOptions(m_lock))
{}

juce::PropertiesFile SharedSettingsFile::open() const
{
    return juce::PropertiesFile{m_file, m_options};
}

} // namespace rock_hero::common::core
