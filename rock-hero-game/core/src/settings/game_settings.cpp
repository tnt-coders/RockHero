#include "rock_hero/game/core/settings/game_settings.h"

#include <filesystem>
#include <rock_hero/common/audio/input/input_device_identity.h>
#include <rock_hero/common/core/shared/application_identity.h>
#include <rock_hero/common/core/shared/json.h>
#include <rock_hero/common/core/shared/juce_path.h>
#include <rock_hero/game/core/audio/game_audio_config.h>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace rock_hero::game::core
{

namespace
{

// Key names are an implementation detail (constraint (b)); consumer plans add keys through the
// port, never by writing to the file directly.
constexpr const char* g_profile_id_key = "profileId";
constexpr const char* g_profile_display_name_key = "profileDisplayName";
constexpr const char* g_first_run_completed_key = "firstRunCompleted";
constexpr const char* g_custom_scan_roots_key = "customScanRoots";
constexpr const char* g_game_audio_config_key = "gameAudioConfig";

// Per-player JSON property names. The route fields reuse the shared audio-config store's spelling
// (audio_config_store.cpp) so one physical route reads the same in either file.
constexpr const char* g_player_slot_property = "playerSlot";
constexpr const char* g_backend_name_property = "backendName";
constexpr const char* g_input_device_name_property = "inputDeviceName";
constexpr const char* g_input_channel_index_property = "inputChannelIndex";
constexpr const char* g_input_channel_name_property = "inputChannelName";

// Display name shown before the user ever sets one.
constexpr const char* g_default_profile_display_name = "Player";

// Custom scan roots persist as a JSON array of native path strings in the single property value,
// so any path characters survive round-trip (the same path bridge the library index store uses).
[[nodiscard]] juce::String encodeScanRoots(std::span<const std::filesystem::path> roots)
{
    juce::var array = common::core::Json::makeArray();
    for (const std::filesystem::path& root : roots)
    {
        array.append(juce::var{common::core::juceStringFromPath(root)});
    }
    return juce::JSON::toString(array);
}

// A corrupt or non-array value reads as no custom roots, mirroring the store's rebuild-on-doubt
// tolerance: a broken settings value must never crash startup.
[[nodiscard]] std::vector<std::filesystem::path> decodeScanRoots(const juce::String& encoded)
{
    std::vector<std::filesystem::path> roots;
    const auto parsed = common::core::Json::parseDocument(encoded);
    if (!parsed.has_value())
    {
        return roots;
    }
    if (const juce::Array<juce::var>* const array = parsed->getArray())
    {
        for (const juce::var& entry : *array)
        {
            const juce::String text = entry.toString();
            if (text.isNotEmpty())
            {
                roots.push_back(common::core::pathFromJuceString(text));
            }
        }
    }
    return roots;
}

// The game audio config persists as a JSON array of player objects in the single property value.
// v1 writes exactly one slot-0 entry, so the multi-input schema is genuinely round-tripped rather
// than a hollow symmetry type.
[[nodiscard]] juce::String encodeGameAudioConfig(const GameAudioConfig& config)
{
    juce::var array = common::core::Json::makeArray();
    for (const PlayerInputConfig& player : config.players)
    {
        array.append(
            common::core::Json::makeObject({
                {g_player_slot_property, player.player_slot},
                {g_backend_name_property,
                 common::core::Json::makeString(player.route.backend_name)},
                {g_input_device_name_property,
                 common::core::Json::makeString(player.route.input_device_name)},
                {g_input_channel_index_property, player.route.input_channel_index},
                {g_input_channel_name_property,
                 common::core::Json::makeString(player.route.input_channel_name)},
            }));
    }
    return juce::JSON::toString(array);
}

// A corrupt or non-array value reads as an empty config, matching the scan-roots tolerance: a
// broken settings value must never crash startup.
[[nodiscard]] GameAudioConfig decodeGameAudioConfig(const juce::String& encoded)
{
    GameAudioConfig config;
    const auto parsed = common::core::Json::parseDocument(encoded);
    if (!parsed.has_value())
    {
        return config;
    }
    const juce::Array<juce::var>* const array = parsed->getArray();
    if (array == nullptr)
    {
        return config;
    }
    for (const juce::var& entry : *array)
    {
        config.players.push_back(
            PlayerInputConfig{
                .player_slot =
                    common::core::Json::readOptionalInt(entry, g_player_slot_property, 0),
                .route = common::audio::InputDeviceIdentity{
                    .backend_name =
                        common::core::Json::readOptionalString(entry, g_backend_name_property),
                    .input_device_name =
                        common::core::Json::readOptionalString(entry, g_input_device_name_property),
                    .input_channel_index = common::core::Json::readOptionalInt(
                        entry, g_input_channel_index_property, -1),
                    .input_channel_name = common::core::Json::readOptionalString(
                        entry, g_input_channel_name_property),
                },
            });
    }
    return config;
}

// Mirrors the editor's settings location policy: per-user application data, shared "Rock Hero"
// folder, XML storage, named by the game application identity.
[[nodiscard]] juce::PropertiesFile::Options gameSettingsOptions()
{
    juce::PropertiesFile::Options options;
    const std::string_view application_name = common::core::gameApplicationName();
    const std::string_view folder_name = common::core::applicationDataFolderName();
    options.applicationName = juce::String{application_name.data(), application_name.size()};
    options.folderName = juce::String{folder_name.data(), folder_name.size()};
    options.filenameSuffix = ".settings";
    options.osxLibrarySubFolder = "Application Support";
    options.storageFormat = juce::PropertiesFile::storeAsXML;
    return options;
}

// Saves immediately so an acknowledged write survives a crash; the one shared failure path.
[[nodiscard]] std::expected<void, GameSettingsError> saveIfNeeded(
    juce::PropertiesFile& properties, std::string message)
{
    if (properties.saveIfNeeded())
    {
        return {};
    }

    return std::unexpected{GameSettingsError{
        GameSettingsErrorCode::CouldNotSave,
        std::move(message),
    }};
}

} // namespace

// Opens the per-user store lazily; a missing file is simply an empty property set.
GameSettings::GameSettings()
    : m_properties{gameSettingsOptions()}
{}

// Test isolation: an explicit file path keeps unit tests away from the real per-user store.
GameSettings::GameSettings(const std::filesystem::path& settings_file)
    : m_properties{common::core::juceFileFromPath(settings_file), gameSettingsOptions()}
{}

// Get-or-create keeps the id stable for the lifetime of the user's data folder: every score
// record stamps it, so it must never regenerate once persisted.
std::expected<std::string, GameSettingsError> GameSettings::getOrCreateProfileId()
{
    const juce::String stored = m_properties.getValue(g_profile_id_key);
    if (stored.isNotEmpty())
    {
        return stored.toStdString();
    }

    const juce::String minted = juce::Uuid{}.toString();
    m_properties.setValue(g_profile_id_key, minted);
    if (const auto saved = saveIfNeeded(m_properties, "Could not save generated profile id.");
        !saved.has_value())
    {
        return std::unexpected{saved.error()};
    }
    return minted.toStdString();
}

// Empty storage reads as the default name; the setter forbids storing an empty name instead.
std::string GameSettings::profileDisplayName() const
{
    const juce::String stored = m_properties.getValue(g_profile_display_name_key);
    return stored.isNotEmpty() ? stored.toStdString() : std::string{g_default_profile_display_name};
}

std::expected<void, GameSettingsError> GameSettings::setProfileDisplayName(
    const std::string& display_name)
{
    if (display_name.empty())
    {
        return std::unexpected{GameSettingsError{
            GameSettingsErrorCode::InvalidSettingValue,
            "Profile display name must not be empty.",
        }};
    }

    m_properties.setValue(g_profile_display_name_key, juce::String{display_name.c_str()});
    return saveIfNeeded(m_properties, "Could not save profile display name.");
}

// Absence means onboarding never completed — plan 26 treats that as first run.
std::optional<bool> GameSettings::firstRunCompleted() const
{
    if (!m_properties.containsKey(g_first_run_completed_key))
    {
        return std::nullopt;
    }
    return m_properties.getBoolValue(g_first_run_completed_key);
}

std::expected<void, GameSettingsError> GameSettings::setFirstRunCompleted(const bool completed)
{
    m_properties.setValue(g_first_run_completed_key, completed);
    return saveIfNeeded(m_properties, "Could not save first-run completion flag.");
}

// Absence reads as no custom roots; the default Songs folder is derived, not stored.
std::vector<std::filesystem::path> GameSettings::customScanRoots() const
{
    if (!m_properties.containsKey(g_custom_scan_roots_key))
    {
        return {};
    }
    return decodeScanRoots(m_properties.getValue(g_custom_scan_roots_key));
}

std::expected<void, GameSettingsError> GameSettings::setCustomScanRoots(
    std::span<const std::filesystem::path> roots)
{
    m_properties.setValue(g_custom_scan_roots_key, encodeScanRoots(roots));
    return saveIfNeeded(m_properties, "Could not save custom scan roots.");
}

// Absence reads as an empty config; the game is simply unconfigured until the player picks a route.
GameAudioConfig GameSettings::gameAudioConfig() const
{
    if (!m_properties.containsKey(g_game_audio_config_key))
    {
        return {};
    }
    return decodeGameAudioConfig(m_properties.getValue(g_game_audio_config_key));
}

std::expected<void, GameSettingsError> GameSettings::setGameAudioConfig(
    const GameAudioConfig& config)
{
    m_properties.setValue(g_game_audio_config_key, encodeGameAudioConfig(config));
    return saveIfNeeded(m_properties, "Could not save game audio config.");
}

} // namespace rock_hero::game::core
