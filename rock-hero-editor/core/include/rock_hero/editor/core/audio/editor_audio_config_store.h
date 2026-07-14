/*!
\file editor_audio_config_store.h
\brief Editor audio-config store that delegates to either the editor's own store or the game's.
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
\brief Points the editor's audio config at either its own store or a read-only view of the game's.

The editor's config store points at either the editor's own read-write store or a read-only view of
the game's config, per the "use game audio settings" toggle. Every read and write delegates to the
active one. Because the game view is opened read-only, any write while sourcing the game fails
loudly rather than silently succeeding or being redirected: a failed write surfaces a caller that
tried to persist while the UI should be treating the game's configuration as read-only.

Freshness is real, not nominal: juce::PropertiesFile parses its file once at construction and serves
getters from memory, so the store reconstructs the read-only game view on each useGameSource(true)
and each gameSourceAvailable() check. This is a one-shot fresh read at those moments, deliberately
not a live file watch.
*/
class EditorAudioConfigStore final : public common::audio::IAudioConfigStore
{
public:
    /*!
    \brief Builds the store over the editor's own store and the game's audio-config file path.
    \param own_store Editor's own read-write audio-config store, active while not sourcing the game.
    \param game_settings_file Native path of the game's audio-config file, opened read-only on demand.
    */
    EditorAudioConfigStore(
        common::audio::IAudioConfigStore& own_store, std::filesystem::path game_settings_file);

    /*! \brief Copying is disabled because the store owns a JUCE-backed read-only view. */
    EditorAudioConfigStore(const EditorAudioConfigStore&) = delete;

    /*! \brief Copy assignment is disabled because the store owns a JUCE-backed read-only view. */
    EditorAudioConfigStore& operator=(const EditorAudioConfigStore&) = delete;

    /*! \brief Moving is disabled so the store keeps a stable address for its injected consumers. */
    EditorAudioConfigStore(EditorAudioConfigStore&&) = delete;

    /*!
    \brief Move assignment is disabled so the store keeps a stable address for its consumers.
    \return Reference to this store.
    */
    EditorAudioConfigStore& operator=(EditorAudioConfigStore&&) = delete;

    /*! \brief Destroys the store. */
    ~EditorAudioConfigStore() override = default;

    /*!
    \brief Selects whether the active source is the game's config or the editor's own.
    \param enabled True to source the game (reconstructing a fresh read-only view), false to source
                   the editor's own store.
    */
    void useGameSource(bool enabled);

    /*!
    \brief Reports which source is currently active.
    \return True while the game's config is the active source.
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
    \brief Reads the active device route from the active source.
    \return Stored route, or empty when none is stored or the stored value is unreadable.
    */
    [[nodiscard]] std::optional<common::audio::ActiveDeviceRoute> activeDeviceRoute()
        const override;

    /*!
    \brief Stores or clears the active device route in the active source.

    While sourcing the game the active source is read-only, so this fails loudly rather than
    persisting the game-adopted route.

    \param route Route to store, or empty to clear.
    \return Empty success, or a typed store failure (including the read-only failure while sourcing
            the game).
    */
    [[nodiscard]] std::expected<void, common::audio::AudioConfigError> setActiveDeviceRoute(
        std::optional<common::audio::ActiveDeviceRoute> route) override;

    /*!
    \brief Reads input calibration for one route from the active source.
    \param identity Physical input route to look up.
    \return Calibration state, absence, or a typed store failure.
    */
    [[nodiscard]] std::expected<
        std::optional<common::audio::InputCalibrationState>, common::audio::AudioConfigError>
    inputCalibrationFor(const common::audio::InputDeviceIdentity& identity) const override;

    /*!
    \brief Stores input calibration in the active source.

    While sourcing the game the active source is read-only, so this fails loudly.

    \param calibration_state Calibration state to save.
    \return Empty success, or a typed store failure (including the read-only failure while sourcing
            the game).
    */
    [[nodiscard]] std::expected<void, common::audio::AudioConfigError> saveInputCalibration(
        common::audio::InputCalibrationState calibration_state) override;

    /*!
    \brief Removes input calibration from the active source.

    While sourcing the game the active source is read-only, so this fails loudly.

    \param identity Physical input route to remove.
    \return Empty success, or a typed store failure (including the read-only failure while sourcing
            the game).
    */
    [[nodiscard]] std::expected<void, common::audio::AudioConfigError> removeInputCalibration(
        const common::audio::InputDeviceIdentity& identity) override;

private:
    // Opens a fresh read-only view of the game's audio-config file. A fresh construction re-reads
    // the file, so callers see the game's current state rather than a snapshot from editor launch.
    [[nodiscard]] std::unique_ptr<common::audio::AudioConfigStore> openGameView() const;

    // The active source: the read-only game view while sourcing the game, otherwise the own store.
    [[nodiscard]] const common::audio::IAudioConfigStore& active() const noexcept;
    [[nodiscard]] common::audio::IAudioConfigStore& active() noexcept;

    // The editor's own read-write store, active while not sourcing the game.
    common::audio::IAudioConfigStore& m_own_store;

    // Native path of the game's audio-config file, reopened read-only on each fresh read.
    std::filesystem::path m_game_settings_file;

    // Non-null exactly while sourcing the game; holds the fresh read-only view every access
    // delegates to. Opened read-only, so a write while sourcing the game fails rather than mutating.
    std::unique_ptr<common::audio::AudioConfigStore> m_game_store;
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
