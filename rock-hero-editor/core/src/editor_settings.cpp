#include "editor_settings.h"

#include <rock_hero/common/core/juce_path.h>

namespace rock_hero::editor::core
{

namespace
{

constexpr const char* g_last_open_project_key{"lastOpenProject"};
constexpr const char* g_interrupted_restore_project_key{"interruptedRestoreProject"};
constexpr const char* g_audio_device_state_key{"audioDeviceState"};
constexpr const char* g_input_calibration_gain_db_key{"inputCalibrationGainDb"};
constexpr const char* g_input_calibration_backend_name_key{"inputCalibrationBackendName"};
constexpr const char* g_input_calibration_input_device_name_key{"inputCalibrationInputDeviceName"};
constexpr const char* g_input_calibration_input_channel_index_key{
    "inputCalibrationInputChannelIndex"
};
constexpr const char* g_input_calibration_input_channel_name_key{
    "inputCalibrationInputChannelName"
};

// Builds the per-user settings file options used by the editor app.
[[nodiscard]] juce::PropertiesFile::Options editorSettingsOptions()
{
    juce::PropertiesFile::Options options;
    options.applicationName = "Rock Hero Editor";
    options.filenameSuffix = ".settings";
    options.folderName = "Rock Hero";
    options.osxLibrarySubFolder = "Application Support";
    options.commonToAllUsers = false;
    options.ignoreCaseOfKeyNames = false;
    options.doNotSave = false;
    options.millisecondsBeforeSaving = 0;
    options.storageFormat = juce::PropertiesFile::storeAsXML;
    options.processLock = nullptr;
    return options;
}

} // namespace

// Opens the JUCE properties file that backs app-local editor settings.
EditorSettings::EditorSettings()
    : m_properties(editorSettingsOptions())
{}

// Opens an explicit settings file so lifecycle behavior can be exercised in isolation.
EditorSettings::EditorSettings(const std::filesystem::path& settings_file)
    : m_properties(common::core::juceFileFromPath(settings_file), editorSettingsOptions())
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
void EditorSettings::setLastOpenProject(std::optional<std::filesystem::path> project_file)
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

    m_properties.saveIfNeeded();
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
void EditorSettings::setInterruptedRestoreProject(std::optional<std::filesystem::path> project_file)
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

    m_properties.saveIfNeeded();
}

// Reads the opaque serialized audio-device state stored by a previous editor session.
std::optional<std::string> EditorSettings::audioDeviceState() const
{
    const juce::String value = m_properties.getValue(g_audio_device_state_key);
    if (value.isEmpty())
    {
        return std::nullopt;
    }

    return value.toStdString();
}

// Stores or clears the opaque serialized audio-device state.
void EditorSettings::setAudioDeviceState(std::optional<std::string> serialized_state)
{
    if (serialized_state.has_value() && !serialized_state->empty())
    {
        m_properties.setValue(g_audio_device_state_key, juce::String{serialized_state->c_str()});
    }
    else
    {
        m_properties.removeValue(g_audio_device_state_key);
    }

    m_properties.saveIfNeeded();
}

// Reads the stored app-local calibration and treats incomplete records as unset.
std::optional<common::audio::InputCalibrationState> EditorSettings::inputCalibrationState() const
{
    if (!m_properties.containsKey(g_input_calibration_gain_db_key) ||
        !m_properties.containsKey(g_input_calibration_input_channel_index_key))
    {
        return std::nullopt;
    }

    common::audio::InputCalibrationState state{
        .calibration_gain = common::audio::clampGain(
            common::audio::Gain{m_properties.getDoubleValue(g_input_calibration_gain_db_key)}),
        .input_device_identity = common::audio::InputDeviceIdentity{
            .backend_name =
                m_properties.getValue(g_input_calibration_backend_name_key).toStdString(),
            .input_device_name =
                m_properties.getValue(g_input_calibration_input_device_name_key).toStdString(),
            .input_channel_index =
                m_properties.getIntValue(g_input_calibration_input_channel_index_key, -1),
            .input_channel_name =
                m_properties.getValue(g_input_calibration_input_channel_name_key).toStdString(),
        },
    };
    if (!common::audio::isValidInputDeviceIdentity(state.input_device_identity))
    {
        return std::nullopt;
    }

    return state;
}

// Stores or clears the app-local calibration record.
void EditorSettings::setInputCalibrationState(
    std::optional<common::audio::InputCalibrationState> calibration_state)
{
    if (calibration_state.has_value() &&
        common::audio::isValidInputDeviceIdentity(calibration_state->input_device_identity))
    {
        const common::audio::InputCalibrationState state{
            .calibration_gain = common::audio::clampGain(calibration_state->calibration_gain),
            .input_device_identity = calibration_state->input_device_identity,
        };
        m_properties.setValue(g_input_calibration_gain_db_key, state.calibration_gain.db);
        m_properties.setValue(
            g_input_calibration_backend_name_key,
            juce::String::fromUTF8(state.input_device_identity.backend_name.c_str()));
        m_properties.setValue(
            g_input_calibration_input_device_name_key,
            juce::String::fromUTF8(state.input_device_identity.input_device_name.c_str()));
        m_properties.setValue(
            g_input_calibration_input_channel_index_key,
            state.input_device_identity.input_channel_index);
        m_properties.setValue(
            g_input_calibration_input_channel_name_key,
            juce::String::fromUTF8(state.input_device_identity.input_channel_name.c_str()));
    }
    else
    {
        m_properties.removeValue(g_input_calibration_gain_db_key);
        m_properties.removeValue(g_input_calibration_backend_name_key);
        m_properties.removeValue(g_input_calibration_input_device_name_key);
        m_properties.removeValue(g_input_calibration_input_channel_index_key);
        m_properties.removeValue(g_input_calibration_input_channel_name_key);
    }

    m_properties.saveIfNeeded();
}

} // namespace rock_hero::editor::core
