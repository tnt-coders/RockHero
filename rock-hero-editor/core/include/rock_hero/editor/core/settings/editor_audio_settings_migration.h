/*!
\file editor_audio_settings_migration.h
\brief One-shot migration of legacy editor audio settings onto the per-app audio-config store.
*/

#pragma once

#include <rock_hero/common/audio/settings/i_audio_config_store.h>

namespace rock_hero::editor::core
{

class EditorSettings;

/*!
\brief Migrates legacy editor audio settings onto the editor's per-app audio-config store.

Copies the obsolete serialized device-state and input-calibration keys from the pre-migration editor
settings file onto the store, then clears them. Copy-not-move: a key is cleared only after its copy
succeeds, and existing store values are never overwritten, so the migration is idempotent and a
reverted build restores the legacy store as authority with no data loss. The device state migrates
with an absent identity, recomputed on the next successful device apply.

\param legacy Editor settings whose legacy audio keys are read and cleared.
\param editor_audio_store Editor audio-config store the legacy values are copied onto.
*/
void migrateEditorAudioSettings(
    EditorSettings& legacy, common::audio::IAudioConfigStore& editor_audio_store);

} // namespace rock_hero::editor::core
