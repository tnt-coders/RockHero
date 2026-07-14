#include "settings/editor_audio_settings_migration.h"

#include "settings/editor_settings.h"

#include <expected>
#include <optional>
#include <rock_hero/common/audio/input/input_calibration_state.h>
#include <rock_hero/common/audio/settings/active_device_route.h>
#include <rock_hero/common/audio/settings/audio_config_error.h>
#include <string>
#include <vector>

namespace rock_hero::editor::core
{

// Copies the legacy device-state and calibration keys onto the store, clearing each only after its
// copy succeeds. Guards on the store already holding a value so a re-run or an app that already
// wrote its own state never overwrites the authoritative store.
void migrateEditorAudioSettings(
    EditorSettings& legacy, common::audio::IAudioConfigStore& editor_audio_store)
{
    if (!editor_audio_store.activeDeviceRoute().has_value())
    {
        const std::optional<std::string> legacy_state = legacy.readLegacyAudioDeviceState();
        if (legacy_state.has_value() && !legacy_state->empty())
        {
            const std::expected<void, common::audio::AudioConfigError> stored =
                editor_audio_store.setActiveDeviceRoute(
                    common::audio::ActiveDeviceRoute{
                        .serialized_state = *legacy_state,
                        .identity = std::nullopt,
                    });
            if (stored.has_value())
            {
                legacy.clearLegacyAudioDeviceState();
            }
        }
    }

    const std::vector<common::audio::InputCalibrationState> legacy_calibrations =
        legacy.readLegacyInputCalibrations();
    if (legacy_calibrations.empty())
    {
        return;
    }

    bool migrated_cleanly = true;
    for (const common::audio::InputCalibrationState& record : legacy_calibrations)
    {
        const std::expected<
            std::optional<common::audio::InputCalibrationState>,
            common::audio::AudioConfigError>
            existing = editor_audio_store.inputCalibrationFor(record.input_device_identity);
        if (!existing.has_value())
        {
            // The store could not read its history; leave the legacy key for a later retry.
            migrated_cleanly = false;
            continue;
        }
        if (existing->has_value())
        {
            // Never overwrite a route the store already knows.
            continue;
        }
        if (!editor_audio_store.saveInputCalibration(record).has_value())
        {
            migrated_cleanly = false;
        }
    }

    if (migrated_cleanly)
    {
        legacy.clearLegacyInputCalibrations();
    }
}

} // namespace rock_hero::editor::core
