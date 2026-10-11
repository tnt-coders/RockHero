/*!
\file shared_settings_file.h
\brief The settings file both products open at once, and the lock that orders their writes.
*/

#pragma once

#include <concepts>
#include <filesystem>
#include <functional>
#include <juce_data_structures/juce_data_structures.h>
#include <optional>
#include <type_traits>
#include <utility>

namespace rock_hero::common::core
{

/*!
\brief One settings file both products read and write while either may be running.

Knows no settings area: each store keeps its own keys and asks this for the file. Because the
other product may write at any moment, nothing here is cached: every operation opens the file
fresh, and every write runs its read-modify-write while holding one inter-process lock, so neither
product overwrites a key the other just wrote. Each composition root builds one per process and
hands it by reference to every store over the file.
*/
class SharedSettingsFile final
{
public:
    /*!
    \brief Names the file; nothing is opened until an operation asks.
    \param file The shared settings file's path; it need not exist yet.
    */
    explicit SharedSettingsFile(const std::filesystem::path& file);

    /*!
    \brief Opens the file as it is on disk now, for reading.

    JUCE takes the lock while it loads, so a read never sees a half-written file.

    \return A fresh properties file; empty when the file does not exist yet.
    */
    [[nodiscard]] juce::PropertiesFile open() const;

    /*!
    \brief Runs one read-modify-write on a fresh copy of the file while holding the lock.

    The lock is held across \p write, so the write must stay short: the other product waits on it.
    \p write saves the properties itself, so it can report a failed save in its own error.

    \param write Callback given the freshly opened properties.
    \return What \p write returned, or empty when the lock could not be taken and \p write did not
            run.
    */
    template <std::invocable<juce::PropertiesFile&> Write>
    [[nodiscard]] std::optional<std::invoke_result_t<Write, juce::PropertiesFile&>> whileLocked(
        Write&& write)
    {
        const juce::InterProcessLock::ScopedLockType held{m_lock};
        if (!held.isLocked())
        {
            return std::nullopt;
        }
        juce::PropertiesFile properties{m_file, m_options};
        return std::invoke(std::forward<Write>(write), properties);
    }

private:
    // The shared file.
    juce::File m_file;

    // Named by the file, so every process that opens the file takes the same lock.
    juce::InterProcessLock m_lock;

    // The shared storage policy, naming m_lock as the file's process lock so JUCE's own loads and
    // saves hold it too; the lock is re-entrant within a process, so they nest inside a write.
    juce::PropertiesFile::Options m_options;
};

} // namespace rock_hero::common::core
