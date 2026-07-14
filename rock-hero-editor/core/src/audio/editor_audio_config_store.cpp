#include "audio/editor_audio_config_store.h"

#include <utility>

namespace rock_hero::editor::core
{

// Binds the own store and remembers the game file path; the game view is opened lazily on demand so
// construction never reads the game's file until a source switch or availability check asks for it.
EditorAudioConfigStore::EditorAudioConfigStore(
    common::audio::IAudioConfigStore& own_store, std::filesystem::path game_settings_file)
    : m_own_store(own_store)
    , m_game_settings_file(std::move(game_settings_file))
{}

// Opens a fresh read-only view so accesses and availability checks read the game's current file
// rather than a snapshot captured at editor launch.
std::unique_ptr<common::audio::AudioConfigStore> EditorAudioConfigStore::openGameView() const
{
    return std::make_unique<common::audio::AudioConfigStore>(
        m_game_settings_file, common::audio::AudioConfigStore::Access::ReadOnly);
}

void EditorAudioConfigStore::useGameSource(bool enabled)
{
    m_game_store = enabled ? openGameView() : nullptr;
}

bool EditorAudioConfigStore::usingGameSource() const noexcept
{
    return m_game_store != nullptr;
}

bool EditorAudioConfigStore::gameSourceAvailable() const
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

const common::audio::IAudioConfigStore& EditorAudioConfigStore::active() const noexcept
{
    return m_game_store != nullptr
               ? static_cast<const common::audio::IAudioConfigStore&>(*m_game_store)
               : m_own_store;
}

common::audio::IAudioConfigStore& EditorAudioConfigStore::active() noexcept
{
    return m_game_store != nullptr ? static_cast<common::audio::IAudioConfigStore&>(*m_game_store)
                                   : m_own_store;
}

std::optional<common::audio::ActiveDeviceRoute> EditorAudioConfigStore::activeDeviceRoute() const
{
    return active().activeDeviceRoute();
}

std::expected<void, common::audio::AudioConfigError> EditorAudioConfigStore::setActiveDeviceRoute(
    std::optional<common::audio::ActiveDeviceRoute> route)
{
    return active().setActiveDeviceRoute(std::move(route));
}

std::expected<std::optional<common::audio::InputCalibrationState>, common::audio::AudioConfigError>
EditorAudioConfigStore::inputCalibrationFor(
    const common::audio::InputDeviceIdentity& identity) const
{
    return active().inputCalibrationFor(identity);
}

std::expected<void, common::audio::AudioConfigError> EditorAudioConfigStore::saveInputCalibration(
    common::audio::InputCalibrationState calibration_state)
{
    return active().saveInputCalibration(std::move(calibration_state));
}

std::expected<void, common::audio::AudioConfigError> EditorAudioConfigStore::removeInputCalibration(
    const common::audio::InputDeviceIdentity& identity)
{
    return active().removeInputCalibration(identity);
}

} // namespace rock_hero::editor::core
