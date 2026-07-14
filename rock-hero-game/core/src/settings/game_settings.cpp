#include "rock_hero/game/core/settings/game_settings.h"

#include <filesystem>
#include <rock_hero/common/core/shared/application_identity.h>
#include <rock_hero/common/core/shared/juce_path.h>
#include <string_view>
#include <utility>

namespace rock_hero::game::core
{

namespace
{

// Key names are an implementation detail (constraint (b)); consumer plans add keys through the
// port, never by writing to the file directly.
constexpr const char* g_profile_id_key = "profileId";
constexpr const char* g_profile_display_name_key = "profileDisplayName";
constexpr const char* g_first_run_completed_key = "firstRunCompleted";

// Display name shown before the user ever sets one.
constexpr const char* g_default_profile_display_name = "Player";

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

} // namespace rock_hero::game::core
