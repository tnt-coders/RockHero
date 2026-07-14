#include "audio/editor_effective_audio_config_store.h"

#include <utility>

namespace rock_hero::editor::core
{

// Binds the own store and remembers the game file path; the game view is opened lazily on demand so
// construction never reads the game's file until a source switch or availability check asks for it.
EditorEffectiveAudioConfigStore::EditorEffectiveAudioConfigStore(
    common::audio::IAudioConfigStore& own_store, std::filesystem::path game_settings_file)
    : m_own_store(own_store)
    , m_game_settings_file(std::move(game_settings_file))
{}

// Opens a fresh read-only view so getters and availability checks read the game's current file
// rather than a snapshot captured at editor launch.
std::unique_ptr<common::audio::AudioConfigStore> EditorEffectiveAudioConfigStore::openGameView()
    const
{
    return std::make_unique<common::audio::AudioConfigStore>(
        m_game_settings_file, common::audio::AudioConfigStore::Access::ReadOnly);
}

void EditorEffectiveAudioConfigStore::useGameSource(bool enabled)
{
    m_game_view = enabled ? openGameView() : nullptr;
}

bool EditorEffectiveAudioConfigStore::usingGameSource() const noexcept
{
    return m_game_view != nullptr;
}

bool EditorEffectiveAudioConfigStore::gameSourceAvailable() const
{
    const std::unique_ptr<common::audio::AudioConfigStore> game_view = openGameView();
    const std::optional<common::audio::ActiveDeviceRoute> route = game_view->activeDeviceRoute();
    if (!route.has_value() || !route->identity.has_value())
    {
        return false;
    }

    const std::expected<
        std::optional<common::audio::InputCalibrationState>,
        common::audio::AudioConfigError>
        calibration = game_view->inputCalibrationFor(*route->identity);
    return calibration.has_value() && calibration->has_value();
}

// Getters read from the game view while sourcing the game, otherwise from the editor's own store.
const common::audio::IAudioConfigStore& EditorEffectiveAudioConfigStore::readSource() const noexcept
{
    return m_game_view != nullptr
               ? static_cast<const common::audio::IAudioConfigStore&>(*m_game_view)
               : m_own_store;
}

std::optional<common::audio::ActiveDeviceRoute> EditorEffectiveAudioConfigStore::activeDeviceRoute()
    const
{
    return readSource().activeDeviceRoute();
}

// Suppressed to a no-op success while sourcing the game so a game-adopted route is never written
// into the editor's own store; routes through to the own store otherwise.
std::expected<void, common::audio::AudioConfigError> EditorEffectiveAudioConfigStore::
    setActiveDeviceRoute(std::optional<common::audio::ActiveDeviceRoute> route)
{
    if (usingGameSource())
    {
        return {};
    }

    return m_own_store.setActiveDeviceRoute(std::move(route));
}

std::expected<std::optional<common::audio::InputCalibrationState>, common::audio::AudioConfigError>
EditorEffectiveAudioConfigStore::inputCalibrationFor(
    const common::audio::InputDeviceIdentity& identity) const
{
    return readSource().inputCalibrationFor(identity);
}

std::expected<void, common::audio::AudioConfigError> EditorEffectiveAudioConfigStore::
    saveInputCalibration(common::audio::InputCalibrationState calibration_state)
{
    return m_own_store.saveInputCalibration(std::move(calibration_state));
}

std::expected<void, common::audio::AudioConfigError> EditorEffectiveAudioConfigStore::
    removeInputCalibration(const common::audio::InputDeviceIdentity& identity)
{
    return m_own_store.removeInputCalibration(identity);
}

} // namespace rock_hero::editor::core
