#include "settings/editor_settings.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <memory>
#include <optional>
#include <rock_hero/common/audio/settings/audio_config_error.h>
#include <rock_hero/common/audio/settings/audio_config_identity.h>
#include <rock_hero/common/core/shared/application_identity.h>
#include <rock_hero/common/core/shared/juce_path.h>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace rock_hero::editor::core
{

namespace
{

constexpr const char* g_last_open_project_key{"lastOpenProject"};
constexpr const char* g_interrupted_restore_project_key{"interruptedRestoreProject"};
constexpr const char* g_waveform_visible_key{"waveformVisible"};
constexpr const char* g_tone_file_directory_key{"toneFileDirectory"};
constexpr const char* g_use_game_audio_settings_key{"useGameAudioSettings"};
constexpr const char* g_suppress_game_audio_recommendation_key{"suppressGameAudioRecommendation"};
constexpr const char* g_tab_minimum_displayed_strings_key{"tabMinimumDisplayedStrings"};

// Builds the per-user settings file options used by the editor app.
[[nodiscard]] juce::PropertiesFile::Options editorSettingsOptions()
{
    juce::PropertiesFile::Options options;
    const std::string_view application_name = common::core::editorApplicationName();
    const std::string_view folder_name = common::core::applicationDataFolderName();
    options.applicationName = juce::String{application_name.data(), application_name.size()};
    options.filenameSuffix = ".settings";
    options.folderName = juce::String{folder_name.data(), folder_name.size()};
    options.osxLibrarySubFolder = "Application Support";
    options.commonToAllUsers = false;
    options.ignoreCaseOfKeyNames = false;
    options.doNotSave = false;
    options.millisecondsBeforeSaving = 0;
    options.storageFormat = juce::PropertiesFile::storeAsXML;
    options.processLock = nullptr;
    return options;
}

// Parses text as a finite double, rejecting empty, malformed, or non-finite input so a corrupt
// entry reads as absent rather than a bogus number. juce::CharacterFunctions rather than
// std::from_chars: Apple's libc++ ships no floating-point from_chars overloads, and the JUCE
// parser is equally locale-independent; the advanced cursor gives the same whole-string
// strictness check.
[[nodiscard]] std::optional<double> parseFiniteDouble(const juce::String& text)
{
    const juce::String trimmed = text.trim();
    if (trimmed.isEmpty())
    {
        return std::nullopt;
    }

    juce::String::CharPointerType cursor = trimmed.getCharPointer();
    const double value = juce::CharacterFunctions::readDoubleValue(cursor);
    if (!cursor.isEmpty() || !std::isfinite(value))
    {
        return std::nullopt;
    }

    return value;
}

// Converts project paths into stable app-settings keys without requiring callers to pre-normalize
// chooser, restore, and test paths. Shared by every per-project record family.
[[nodiscard]] std::filesystem::path projectSettingsKeyFor(const std::filesystem::path& project_file)
{
    if (project_file.empty())
    {
        return {};
    }

    std::error_code error;
    const std::filesystem::path canonical = std::filesystem::weakly_canonical(project_file, error);
    if (!error && !canonical.empty())
    {
        return canonical.lexically_normal();
    }

    return project_file.lexically_normal();
}

// One flat settings property per (family, project): key = "<family>:" + normalized project path,
// value = one string. Each project's value is independent, so a corrupt or missing entry resets
// only that value rather than a shared list. Family names double as the stored key prefix.
constexpr std::string_view g_project_cursor_family{"projectCursor"};
constexpr std::string_view g_project_grid_note_value_family{"projectGridNoteValue"};
constexpr std::string_view g_project_timeline_zoom_family{"projectTimelineZoom"};
constexpr std::string_view g_project_selected_arrangement_family{"projectSelectedArrangement"};

// Builds the flat settings key holding one family's value for one project path. Paths run through
// projectSettingsKeyFor so chooser, restore, and test spellings of the same .rhp resolve to one
// record; an empty project path yields an empty key so pathless writes never share a record.
[[nodiscard]] juce::String projectSettingKey(
    std::string_view family, const std::filesystem::path& project_file)
{
    const std::filesystem::path key = projectSettingsKeyFor(project_file);
    if (key.empty())
    {
        return {};
    }

    return juce::String::fromUTF8(family.data(), static_cast<int>(family.size())) + ":" +
           common::core::juceStringFromPath(key);
}

// Parses a stored "numerator/denominator" token as a grid note value, rejecting malformed text or
// non-positive terms so a corrupt entry reads as absent.
[[nodiscard]] std::optional<common::core::Fraction> parseGridNoteValue(const juce::String& text)
{
    const std::string token = text.toStdString();
    const std::size_t slash = token.find('/');
    if (slash == std::string::npos)
    {
        return std::nullopt;
    }

    const auto parse_positive_int = [](std::string_view part) -> std::optional<int> {
        if (part.empty())
        {
            return std::nullopt;
        }
        int value{};
        const char* const begin = part.data();
        const char* const end = begin + part.size();
        const auto [parsed_to, error] = std::from_chars(begin, end, value);
        if (error != std::errc{} || parsed_to != end || value < 1)
        {
            return std::nullopt;
        }
        return value;
    };

    const std::optional<int> numerator =
        parse_positive_int(std::string_view{token}.substr(0, slash));
    const std::optional<int> denominator =
        parse_positive_int(std::string_view{token}.substr(slash + 1));
    if (!numerator.has_value() || !denominator.has_value())
    {
        return std::nullopt;
    }

    return common::core::Fraction{*numerator, *denominator};
}

// Saves pending changes and translates JUCE persistence failure into the settings domain.
[[nodiscard]] std::expected<void, EditorSettingsError> saveIfNeeded(
    juce::PropertiesFile& properties, std::string message)
{
    if (properties.saveIfNeeded())
    {
        return {};
    }

    return std::unexpected{
        EditorSettingsError{EditorSettingsErrorCode::CouldNotSave, std::move(message)}
    };
}

// Forces a settings-file write when callers need to know whether a staged update reached disk.
[[nodiscard]] std::expected<void, EditorSettingsError> saveNow(
    juce::PropertiesFile& properties, std::string message)
{
    if (properties.save())
    {
        return {};
    }

    return std::unexpected{
        EditorSettingsError{EditorSettingsErrorCode::CouldNotSave, std::move(message)}
    };
}

} // namespace

// Opens the JUCE properties file plus the owned per-app audio-config store. The store uses the
// editor audio-config application name so it partitions from the workflow-state file.
EditorSettings::EditorSettings()
    : m_properties(editorSettingsOptions())
    , m_audio_config_store(
          common::audio::editorAudioConfigApplicationName(),
          common::audio::AudioConfigStore::Access::ReadWrite)
{}

// Opens an explicit settings file so lifecycle behavior can be exercised in isolation. The owned
// store opens at a sibling path so the two files never share a writer.
EditorSettings::EditorSettings(const std::filesystem::path& settings_file)
    : m_properties(common::core::juceFileFromPath(settings_file), editorSettingsOptions())
    , m_audio_config_store(
          audioConfigFileFor(settings_file), common::audio::AudioConfigStore::Access::ReadWrite)
{}

// Reads the last editor project path stored by a previous allowed editor exit.
std::optional<std::filesystem::path> EditorSettings::lastOpenProject() const
{
    const juce::String value = m_properties.getValue(g_last_open_project_key);
    if (value.isEmpty())
    {
        return std::nullopt;
    }

    return common::core::pathFromJuceString(value);
}

// Stores or clears the editor project path to restore on the next editor launch.
std::expected<void, EditorSettingsError> EditorSettings::setLastOpenProject(
    std::optional<std::filesystem::path> project_file)
{
    if (project_file.has_value() && !project_file->empty())
    {
        m_properties.setValue(
            g_last_open_project_key, common::core::juceStringFromPath(*project_file));
    }
    else
    {
        m_properties.removeValue(g_last_open_project_key);
    }

    return saveIfNeeded(m_properties, "Could not save last open project setting.");
}

// Reads the project path whose previous startup restore was interrupted before completion.
std::optional<std::filesystem::path> EditorSettings::interruptedRestoreProject() const
{
    const juce::String value = m_properties.getValue(g_interrupted_restore_project_key);
    if (value.isEmpty())
    {
        return std::nullopt;
    }

    return common::core::pathFromJuceString(value);
}

// Stores or clears the startup-restore interruption marker used to avoid retry loops.
std::expected<void, EditorSettingsError> EditorSettings::setInterruptedRestoreProject(
    std::optional<std::filesystem::path> project_file)
{
    if (project_file.has_value() && !project_file->empty())
    {
        m_properties.setValue(
            g_interrupted_restore_project_key, common::core::juceStringFromPath(*project_file));
    }
    else
    {
        m_properties.removeValue(g_interrupted_restore_project_key);
    }

    return saveIfNeeded(m_properties, "Could not save interrupted project restore setting.");
}

// Reads the app-wide waveform visibility preference for the timeline's tablature lane.
std::optional<bool> EditorSettings::waveformVisible() const
{
    if (!m_properties.containsKey(g_waveform_visible_key))
    {
        return std::nullopt;
    }

    return m_properties.getBoolValue(g_waveform_visible_key);
}

// Stores the app-wide waveform visibility preference.
std::expected<void, EditorSettingsError> EditorSettings::setWaveformVisible(bool visible)
{
    m_properties.setValue(g_waveform_visible_key, visible);
    return saveIfNeeded(m_properties, "Could not save waveform visibility setting.");
}

// Reads the directory the tone-file choosers should start in.
std::optional<std::filesystem::path> EditorSettings::toneFileDirectory() const
{
    const juce::String value = m_properties.getValue(g_tone_file_directory_key);
    if (value.isEmpty())
    {
        return std::nullopt;
    }

    return common::core::pathFromJuceString(value);
}

// Stores the directory the tone-file choosers should start in.
std::expected<void, EditorSettingsError> EditorSettings::setToneFileDirectory(
    std::filesystem::path directory)
{
    if (!directory.empty())
    {
        m_properties.setValue(
            g_tone_file_directory_key, common::core::juceStringFromPath(directory));
    }
    else
    {
        m_properties.removeValue(g_tone_file_directory_key);
    }

    return saveIfNeeded(m_properties, "Could not save tone file directory setting.");
}

// Reads whether the editor sources the game's audio configuration instead of its own. Absence is
// preserved so useGameAudioSettingsOrDefault can apply the off default rather than a stored value.
std::optional<bool> EditorSettings::useGameAudioSettings() const
{
    if (!m_properties.containsKey(g_use_game_audio_settings_key))
    {
        return std::nullopt;
    }

    return m_properties.getBoolValue(g_use_game_audio_settings_key);
}

// Stores whether the editor sources the game's audio configuration instead of its own.
std::expected<void, EditorSettingsError> EditorSettings::setUseGameAudioSettings(bool enabled)
{
    m_properties.setValue(g_use_game_audio_settings_key, enabled);
    return saveIfNeeded(m_properties, "Could not save use-game-audio-settings preference.");
}

// Reads whether the startup game-audio recommendation prompt is suppressed.
std::optional<bool> EditorSettings::suppressGameAudioRecommendation() const
{
    if (!m_properties.containsKey(g_suppress_game_audio_recommendation_key))
    {
        return std::nullopt;
    }

    return m_properties.getBoolValue(g_suppress_game_audio_recommendation_key);
}

// Stores whether the startup game-audio recommendation prompt is suppressed.
std::expected<void, EditorSettingsError> EditorSettings::setSuppressGameAudioRecommendation(
    bool suppressed)
{
    m_properties.setValue(g_suppress_game_audio_recommendation_key, suppressed);
    return saveIfNeeded(
        m_properties, "Could not save game-audio recommendation suppression preference.");
}

// Reads the app-wide minimum number of tablature string lanes to display.
std::optional<int> EditorSettings::tabMinimumDisplayedStrings() const
{
    if (!m_properties.containsKey(g_tab_minimum_displayed_strings_key))
    {
        return std::nullopt;
    }

    return m_properties.getIntValue(g_tab_minimum_displayed_strings_key);
}

// Stores the app-wide minimum number of tablature string lanes to display.
std::expected<void, EditorSettingsError> EditorSettings::setTabMinimumDisplayedStrings(
    int minimum_strings)
{
    if (minimum_strings < 0)
    {
        return std::unexpected{EditorSettingsError{
            EditorSettingsErrorCode::InvalidSettingValue,
            "Cannot save a negative tablature string display minimum."
        }};
    }

    m_properties.setValue(g_tab_minimum_displayed_strings_key, minimum_strings);
    return saveIfNeeded(m_properties, "Could not save tablature string display setting.");
}

// Reads the app-local resume caret associated with one project path: the exact musical address
// encoded as "measure:beat:offset_numerator/offset_denominator:string". A missing or
// unparseable entry (including any pre-caret-model seconds value) reads as absent.
std::optional<EditorProjectCaret> EditorSettings::projectCaretFor(
    const std::filesystem::path& project_file) const
{
    const juce::String key = projectSettingKey(g_project_cursor_family, project_file);
    if (key.isEmpty() || !m_properties.containsKey(key))
    {
        return std::nullopt;
    }

    const juce::StringArray parts =
        juce::StringArray::fromTokens(m_properties.getValue(key), ":", "");
    if (parts.size() != 4)
    {
        return std::nullopt;
    }
    const juce::StringArray offset_parts = juce::StringArray::fromTokens(parts[2], "/", "");
    if (offset_parts.size() != 2)
    {
        return std::nullopt;
    }

    const EditorProjectCaret caret{
        .position =
            common::core::GridPosition{
                .measure = parts[0].getIntValue(),
                .beat = parts[1].getIntValue(),
                .offset =
                    common::core::Fraction{
                        offset_parts[0].getIntValue(), offset_parts[1].getIntValue()
                    },
            },
        .string = parts[3].getIntValue(),
    };
    if (caret.position.measure < 1 || caret.position.beat < 1 ||
        caret.position.offset.denominator <= 0 || caret.position.offset.numerator < 0 ||
        caret.string < 1)
    {
        return std::nullopt;
    }
    return caret;
}

// Stores one project's app-local resume caret under its own flat key, as the exact musical
// address (never a time value: no tempo edit can happen without an open session, so the
// address always lands the caret back on the same grid slot).
std::expected<void, EditorSettingsError> EditorSettings::saveProjectCaret(
    const std::filesystem::path& project_file, const EditorProjectCaret& caret)
{
    const juce::String key = projectSettingKey(g_project_cursor_family, project_file);
    if (key.isEmpty() || caret.position.measure < 1 || caret.position.beat < 1 ||
        caret.position.offset.denominator <= 0 || caret.position.offset.numerator < 0 ||
        caret.string < 1)
    {
        return std::unexpected{EditorSettingsError{
            EditorSettingsErrorCode::InvalidSettingValue,
            "Cannot save a project caret for an invalid project path or address."
        }};
    }

    m_properties.setValue(
        key,
        juce::String(caret.position.measure) + ":" + juce::String(caret.position.beat) + ":" +
            juce::String(caret.position.offset.numerator) + "/" +
            juce::String(caret.position.offset.denominator) + ":" + juce::String(caret.string));
    return saveNow(m_properties, "Could not save project caret setting.");
}

// Reads the app-local timeline grid note value associated with one project path.
std::optional<common::core::Fraction> EditorSettings::projectGridNoteValueFor(
    const std::filesystem::path& project_file) const
{
    const juce::String key = projectSettingKey(g_project_grid_note_value_family, project_file);
    if (key.isEmpty() || !m_properties.containsKey(key))
    {
        return std::nullopt;
    }

    return parseGridNoteValue(m_properties.getValue(key));
}

// Stores one project's app-local grid note value under its own flat key.
std::expected<void, EditorSettingsError> EditorSettings::saveProjectGridNoteValue(
    const std::filesystem::path& project_file, common::core::Fraction grid_note_value)
{
    const juce::String key = projectSettingKey(g_project_grid_note_value_family, project_file);
    if (key.isEmpty() || grid_note_value.numerator < 1 || grid_note_value.denominator < 1)
    {
        return std::unexpected{EditorSettingsError{
            EditorSettingsErrorCode::InvalidSettingValue,
            "Cannot save a grid note value for an invalid project path or note value."
        }};
    }

    m_properties.setValue(
        key,
        juce::String(grid_note_value.numerator) + "/" + juce::String(grid_note_value.denominator));
    return saveNow(m_properties, "Could not save project grid note value setting.");
}

// Reads the app-local timeline zoom associated with one project path.
std::optional<double> EditorSettings::projectTimelineZoomFor(
    const std::filesystem::path& project_file) const
{
    const juce::String key = projectSettingKey(g_project_timeline_zoom_family, project_file);
    if (key.isEmpty() || !m_properties.containsKey(key))
    {
        return std::nullopt;
    }

    const std::optional<double> pixels_per_second = parseFiniteDouble(m_properties.getValue(key));
    if (!pixels_per_second.has_value() || *pixels_per_second <= 0.0)
    {
        return std::nullopt;
    }

    return pixels_per_second;
}

// Stores one project's app-local timeline zoom under its own flat key.
std::expected<void, EditorSettingsError> EditorSettings::saveProjectTimelineZoom(
    const std::filesystem::path& project_file, double pixels_per_second)
{
    const juce::String key = projectSettingKey(g_project_timeline_zoom_family, project_file);
    if (key.isEmpty() || !std::isfinite(pixels_per_second) || pixels_per_second <= 0.0)
    {
        return std::unexpected{EditorSettingsError{
            EditorSettingsErrorCode::InvalidSettingValue,
            "Cannot save a timeline zoom for an invalid project path or scale."
        }};
    }

    m_properties.setValue(key, juce::String(pixels_per_second));
    return saveNow(m_properties, "Could not save project timeline zoom setting.");
}

// Reads the app-local displayed-arrangement choice associated with one project path.
std::optional<std::string> EditorSettings::projectSelectedArrangementFor(
    const std::filesystem::path& project_file) const
{
    const juce::String key = projectSettingKey(g_project_selected_arrangement_family, project_file);
    if (key.isEmpty() || !m_properties.containsKey(key))
    {
        return std::nullopt;
    }

    const juce::String value = m_properties.getValue(key);
    if (value.isEmpty())
    {
        return std::nullopt;
    }

    return value.toStdString();
}

// Stores one project's app-local displayed arrangement under its own flat key.
std::expected<void, EditorSettingsError> EditorSettings::saveProjectSelectedArrangement(
    const std::filesystem::path& project_file, std::string arrangement_id)
{
    const juce::String key = projectSettingKey(g_project_selected_arrangement_family, project_file);
    if (key.isEmpty() || arrangement_id.empty())
    {
        return std::unexpected{EditorSettingsError{
            EditorSettingsErrorCode::InvalidSettingValue,
            "Cannot save a selected arrangement for an invalid project path or empty id."
        }};
    }

    m_properties.setValue(key, juce::String::fromUTF8(arrangement_id.c_str()));
    return saveNow(m_properties, "Could not save project selected-arrangement setting.");
}

// Exposes the owned store so app composition can inject it into the controller's device-route path.
common::audio::IAudioConfigStore& EditorSettings::audioConfigStore() noexcept
{
    return m_audio_config_store;
}

// Derives the sibling audio-config file for an explicit settings path, mirroring the production
// "Rock Hero Editor" -> "Rock Hero Editor Audio" partition so the two files never share a writer.
std::filesystem::path EditorSettings::audioConfigFileFor(const std::filesystem::path& settings_file)
{
    std::filesystem::path file_name = settings_file.stem();
    file_name += " Audio";
    file_name += settings_file.extension();
    return settings_file.parent_path() / file_name;
}

} // namespace rock_hero::editor::core
