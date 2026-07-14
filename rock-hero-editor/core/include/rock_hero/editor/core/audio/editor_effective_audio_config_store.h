/*!
\file editor_effective_audio_config_store.h
\brief Editor effective-source audio-config facade: read own store or the game's, write own store.
*/

#pragma once

#include <expected>
#include <filesystem>
#include <memory>
#include <optional>
#include <rock_hero/common/audio/input/input_calibration_state.h>
#include <rock_hero/common/audio/input/input_device_identity.h>
#include <rock_hero/common/audio/settings/active_device_route.h>
#include <rock_hero/common/audio/settings/audio_config_error.h>
#include <rock_hero/common/audio/settings/audio_config_store.h>
#include <rock_hero/common/audio/settings/i_audio_config_store.h>
#include <rock_hero/editor/core/settings/i_editor_settings.h>

namespace rock_hero::editor::core
{

/*!
\brief Presents either the editor's own audio config or a read-only view of the game's as one store.

The editor toggles a "use game settings" source without the rest of the app knowing which side is
live: getters delegate to the editor's own store or to a freshly reconstructed read-only view of the
game's audio-config file, per usingGameSource(). Calibration writes always target the editor's own
store so a save never fails merely because the game source is active. The active device route setter
is the one write suppressed while the game source is active: the adopted route came from the game,
not from a user edit of the editor's own route, so persisting it would overwrite the very route the
editor must restore when the source is switched back. Suppressing it in the facade keeps that
guarantee structural rather than relying on every caller (the controller's device-route persist and
its restore-failure clear both flow through this setter) to remember it.

Freshness is real, not nominal: juce::PropertiesFile parses its file once at construction and serves
getters from memory, so the facade reconstructs the read-only game view on each useGameSource(true)
and each gameSourceAvailable() check. This is a one-shot fresh read at those moments, deliberately
not a live file watch. The game view is opened AudioConfigStore::Access::ReadOnly, so even a stray
write can never mutate the game's file.
*/
class EditorEffectiveAudioConfigStore final : public common::audio::IAudioConfigStore
{
public:
    /*!
    \brief Builds the facade over the editor's own store and the game's audio-config file path.
    \param own_store Editor's own read-write audio-config store; the sole write target.
    \param game_settings_file Native path of the game's audio-config file, opened read-only on demand.
    */
    EditorEffectiveAudioConfigStore(
        common::audio::IAudioConfigStore& own_store, std::filesystem::path game_settings_file);

    /*! \brief Copying is disabled because the facade owns a JUCE-backed read-only view. */
    EditorEffectiveAudioConfigStore(const EditorEffectiveAudioConfigStore&) = delete;

    /*! \brief Copy assignment is disabled because the facade owns a JUCE-backed read-only view. */
    EditorEffectiveAudioConfigStore& operator=(const EditorEffectiveAudioConfigStore&) = delete;

    /*! \brief Moving is disabled so the facade keeps a stable address for its injected consumers. */
    EditorEffectiveAudioConfigStore(EditorEffectiveAudioConfigStore&&) = delete;

    /*!
    \brief Move assignment is disabled so the facade keeps a stable address for its consumers.
    \return Reference to this facade.
    */
    EditorEffectiveAudioConfigStore& operator=(EditorEffectiveAudioConfigStore&&) = delete;

    /*! \brief Destroys the facade. */
    ~EditorEffectiveAudioConfigStore() override = default;

    /*!
    \brief Selects whether getters read the game's config or the editor's own.
    \param enabled True to source the game (reconstructing a fresh read-only view), false to source
                   the editor's own store.
    */
    void useGameSource(bool enabled);

    /*!
    \brief Reports which source getters currently read.
    \return True while getters read the game's config.
    */
    [[nodiscard]] bool usingGameSource() const noexcept;

    /*!
    \brief Reports whether the game's config can be adopted, from a freshly read game view.

    Availability is calibration-aware, not device-presence: it requires the game's stored active
    route to name a resolved input identity AND a matching calibration to exist for exactly that
    route, so a game that recorded a device but never calibrated it does not arm a silent, locked,
    dead source.

    \return True when the game's active route has a resolved identity with a matching calibration.
    */
    [[nodiscard]] bool gameSourceAvailable() const;

    /*!
    \brief Reads the active device route from the selected source.
    \return Stored route, or empty when none is stored or the stored value is unreadable.
    */
    [[nodiscard]] std::optional<common::audio::ActiveDeviceRoute> activeDeviceRoute()
        const override;

    /*!
    \brief Stores or clears the editor's own active device route, unless the game source is active.

    While the game source is active this is a no-op success: the live route was adopted from the
    game, so persisting it into the editor's own store would corrupt the route restored on switch
    back. When the editor's own source is active this writes through to the own store.

    \param route Route to store, or empty to clear.
    \return Empty success (including the suppressed no-op), or a typed store failure from the own store.
    */
    [[nodiscard]] std::expected<void, common::audio::AudioConfigError> setActiveDeviceRoute(
        std::optional<common::audio::ActiveDeviceRoute> route) override;

    /*!
    \brief Reads input calibration for one route from the selected source.
    \param identity Physical input route to look up.
    \return Calibration state, absence, or a typed store failure.
    */
    [[nodiscard]] std::expected<
        std::optional<common::audio::InputCalibrationState>, common::audio::AudioConfigError>
    inputCalibrationFor(const common::audio::InputDeviceIdentity& identity) const override;

    /*!
    \brief Stores input calibration in the editor's own store, regardless of the active source.
    \param calibration_state Calibration state to save.
    \return Empty success, or a typed store failure from the own store.
    */
    [[nodiscard]] std::expected<void, common::audio::AudioConfigError> saveInputCalibration(
        common::audio::InputCalibrationState calibration_state) override;

    /*!
    \brief Removes input calibration from the editor's own store, regardless of the active source.
    \param identity Physical input route to remove.
    \return Empty success, or a typed store failure from the own store.
    */
    [[nodiscard]] std::expected<void, common::audio::AudioConfigError> removeInputCalibration(
        const common::audio::InputDeviceIdentity& identity) override;

private:
    // Opens a fresh read-only view of the game's audio-config file. A fresh construction re-reads
    // the file, so callers see the game's current state rather than a snapshot from editor launch.
    [[nodiscard]] std::unique_ptr<common::audio::AudioConfigStore> openGameView() const;

    // The source getters read from: the fresh game view while sourcing the game, else the own store.
    [[nodiscard]] const common::audio::IAudioConfigStore& readSource() const noexcept;

    // The editor's own read-write store; the sole write target for every setter.
    common::audio::IAudioConfigStore& m_own_store;

    // Native path of the game's audio-config file, reopened read-only on each fresh read.
    std::filesystem::path m_game_settings_file;

    // Non-null exactly while sourcing the game; holds the fresh read-only view getters read from.
    std::unique_ptr<common::audio::AudioConfigStore> m_game_view;
};

/*!
\brief Resolves the "use game audio settings" toggle, defaulting to on at first run.

The persisted toggle is absent until the user first flips it; an absent value resolves to on so a
fresh editor adopts the game's audio configuration by default, while a written value (on or off) is
authoritative on every later launch.

\param settings Editor settings holding the persisted toggle.
\return True when the editor should source the game's audio configuration.
*/
[[nodiscard]] inline bool useGameAudioSettingsOrDefault(const IEditorSettings& settings)
{
    return settings.useGameAudioSettings().value_or(true);
}

} // namespace rock_hero::editor::core
