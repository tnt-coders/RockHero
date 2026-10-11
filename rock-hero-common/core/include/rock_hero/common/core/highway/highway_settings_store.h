/*!
\file highway_settings_store.h
\brief The player's highway settings, stored in the settings file both products share.
*/

#pragma once

#include <expected>
#include <functional>
#include <rock_hero/common/core/highway/highway_player_settings.h>
#include <rock_hero/common/core/highway/highway_settings_error.h>
#include <rock_hero/common/core/shared/shared_settings_file.h>

namespace rock_hero::common::core
{

/*!
\brief Stores the player's highway settings as flat `highway.*` keys of the shared settings file.

Each setting is its own key, so one that is absent or unreadable reads as its default without
disturbing the others. Both products may run at once and either may change a setting, so nothing
is cached and there is no whole-record write: a record cached by one product would overwrite what
the other changed since. A change is a function applied to the record as stored at that moment.
*/
class HighwaySettingsStore final
{
public:
    /*!
    \brief Stores the highway settings in the shared file.
    \param settings_file The file both products share; must outlive the store.
    */
    explicit HighwaySettingsStore(SharedSettingsFile& settings_file);

    /*!
    \brief Reads the settings as stored now.
    \return The stored settings; any setting that is absent or unreadable reads as its default.
    */
    [[nodiscard]] HighwayPlayerSettings read() const;

    /*!
    \brief Applies one change to the stored settings and stores the result.

    The read, the change and the write happen under the shared file's lock, so the other product
    cannot interleave with them; \p change must be short and must not call back into the store.

    \param change Edits the record it is given, which is the stored record at that moment.
    \return The settings as stored after the change, or CouldNotSave when the file could not be
            locked or written.
    */
    [[nodiscard]] std::expected<HighwayPlayerSettings, HighwaySettingsError> update(
        const std::function<void(HighwayPlayerSettings&)>& change);

private:
    // The file both products share; the composition root owns it.
    SharedSettingsFile& m_settings_file;
};

} // namespace rock_hero::common::core
