#include "engine.h"

#include "tracktion_instrument_wave_device_mapping.h"
#include "tracktion_thumbnail.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <exception>
#include <expected>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <rock_hero/common/core/json.h>
#include <rock_hero/common/core/package_id.h>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <tracktion_engine/tracktion_engine.h>
#include <utility>
#include <vector>

namespace rock_hero::common::audio
{

namespace
{

constexpr std::string_view g_tone_state_directory_name{"state"};
constexpr std::string_view g_plugin_state_extension{".tracktion-plugin"};
constexpr std::string_view g_plugin_scan_command_line_prefix{"--PluginScan:"};
constexpr auto g_plugin_scan_timeout = std::chrono::seconds{30};

// Cancels Tracktion's custom plugin scanner if a child-process scan never replies.
class PluginScanTimeout final
{
public:
    PluginScanTimeout(std::function<void()> abort_scan, std::chrono::milliseconds timeout)
        : m_abort_scan(std::move(abort_scan))
        , m_thread([this, timeout] {
            std::unique_lock lock{m_mutex};
            if (m_finished_condition.wait_for(lock, timeout, [this] { return m_finished; }))
            {
                return;
            }

            lock.unlock();
            m_timed_out.store(true);
            if (m_abort_scan)
            {
                m_abort_scan();
            }
        })
    {}

    ~PluginScanTimeout()
    {
        finish();
    }

    PluginScanTimeout(const PluginScanTimeout&) = delete;
    PluginScanTimeout& operator=(const PluginScanTimeout&) = delete;
    PluginScanTimeout(PluginScanTimeout&&) = delete;
    PluginScanTimeout& operator=(PluginScanTimeout&&) = delete;

    void finish()
    {
        {
            const std::scoped_lock lock{m_mutex};
            m_finished = true;
        }

        m_finished_condition.notify_one();

        if (m_thread.joinable())
        {
            m_thread.join();
        }
    }

    [[nodiscard]] bool timedOut() const noexcept
    {
        return m_timed_out.load();
    }

private:
    std::function<void()> m_abort_scan;
    std::mutex m_mutex;
    std::condition_variable m_finished_condition;
    bool m_finished{false};
    std::atomic_bool m_timed_out{false};
    std::thread m_thread;
};

// Serializable plugin identity stored in the audio-owned tone document.
struct PluginIdentity
{
    std::string format_name;
    std::string name;
    std::string descriptive_name;
    std::string manufacturer;
    std::string version;
    std::string unique_id;
    std::string deprecated_uid;
    bool is_instrument{false};
    std::string original_file_or_identifier;
    std::string juce_identifier_hint;
    std::string tracktion_identifier_hint;
};

// One persisted plugin entry in the default linear live rig chain.
struct PluginRecord
{
    std::string id;
    PluginIdentity identity;
    std::string tracktion_state_ref;
};

// V1 tone document subset currently used by the linear plugin-chain runtime.
struct ToneDocument
{
    std::vector<PluginRecord> chain;
};

// Per-call state for an in-flight async live rig load. Lives on the heap inside Engine::Impl so
// MessageManager::callAsync continuations can resume the work between plugins without each lambda
// having to carry the full state along.
struct LiveRigLoadOperation
{
    // Original request, kept for the song directory and the progress callback.
    LiveRigLoadRequest request;

    // Parsed tone document chain, already validated against package-relative path rules.
    std::vector<PluginRecord> chain;

    // Display names precomputed once so progress messages stay stable across resume points.
    std::vector<std::string> display_names;

    // Final result delivered to the caller once every plugin is restored.
    LiveRigLoadResult result;

    // Index of the next plugin to load on the upcoming step.
    std::size_t next_index{0};

    // Callback fired exactly once when the load finishes or fails.
    LiveRigLoadResultCallback on_result;
};

// Records recoverable instrument-route failures without turning an internal bind into a public
// error.
void logInstrumentMonitoringFailure(const juce::String& message)
{
    juce::Logger::writeToLog("Rock Hero instrument monitoring: " + message);
}

// Converts a standard filesystem path into the JUCE file type required by Tracktion/JUCE APIs.
[[nodiscard]] juce::File toJuceFile(const std::filesystem::path& path)
{
    const auto path_text = path.wstring();
    return juce::File{juce::String{path_text.c_str()}};
}

// Converts UTF-8-ish command line text from the public startup boundary into JUCE text.
[[nodiscard]] juce::String toJuceString(std::string_view text)
{
    const std::string utf8_text{text};
    return text.empty() ? juce::String{} : juce::String::fromUTF8(utf8_text.c_str());
}

// Converts signed JUCE plugin IDs into the hex text used by JUCE and Tracktion state.
[[nodiscard]] std::string toHexString(int value)
{
    return juce::String::toHexString(value).toStdString();
}

// Converts hex text from tone JSON back to the JUCE plugin ID integer domain.
[[nodiscard]] int fromHexString(const std::string& value)
{
    return static_cast<int>(juce::String::fromUTF8(value.c_str()).getHexValue64());
}

// Reports whether a package-relative reference stays inside the song workspace.
[[nodiscard]] bool isSafeRelativePath(const std::filesystem::path& path)
{
    if (path.empty() || path.is_absolute() || path.has_root_name() || path.has_root_directory())
    {
        return false;
    }

    for (const std::filesystem::path& part : path)
    {
        const std::string text = part.generic_string();
        if (text.empty() || text == "." || text == ".." || text.find(':') != std::string::npos)
        {
            return false;
        }
    }

    return true;
}

// Resolves a package-relative file and verifies it exists inside the song workspace.
[[nodiscard]] std::optional<std::filesystem::path> resolvePackageFile(
    const std::filesystem::path& song_directory, const std::string& relative_path)
{
    const std::filesystem::path package_path{relative_path};
    if (!isSafeRelativePath(package_path))
    {
        return std::nullopt;
    }

    std::filesystem::path resolved_path = (song_directory / package_path).lexically_normal();
    std::error_code error;
    if (!std::filesystem::is_regular_file(resolved_path, error))
    {
        return std::nullopt;
    }

    return resolved_path;
}

// Builds the generated package-relative tone document path for a new tone.
[[nodiscard]] std::filesystem::path generatedToneDocumentPath()
{
    const std::string tone_id = core::generatePackageId();
    return std::filesystem::path{core::toneDocumentRefForToneId(tone_id)};
}

// Derives the stable state-file namespace owned by a tone document path.
[[nodiscard]] std::filesystem::path toneDocumentStateDirectory(
    const std::filesystem::path& tone_document_ref)
{
    return tone_document_ref.parent_path() / std::filesystem::path{g_tone_state_directory_name};
}

// Builds one package-relative Tracktion plugin-state sidecar path.
[[nodiscard]] std::filesystem::path generatedPluginStatePath(
    const std::filesystem::path& state_directory, std::size_t plugin_index)
{
    return state_directory /
           ("plugin-" + std::to_string(plugin_index + 1) + std::string{g_plugin_state_extension});
}

// Reports whether a Tracktion state sidecar stays in the tone document's co-located state folder.
[[nodiscard]] bool isCanonicalPluginStateRef(
    const std::string& plugin_state_ref, const std::filesystem::path& expected_state_directory)
{
    const std::filesystem::path plugin_state_path{plugin_state_ref};
    const std::string extension = plugin_state_path.extension().generic_string();
    return isSafeRelativePath(plugin_state_path) &&
           plugin_state_path.parent_path().generic_string() ==
               expected_state_directory.generic_string() &&
           extension == g_plugin_state_extension;
}

// Creates directories needed for a package-relative output file.
[[nodiscard]] std::optional<LiveRigError> createParentDirectory(
    const std::filesystem::path& output_file)
{
    std::error_code error;
    std::filesystem::create_directories(output_file.parent_path(), error);
    if (error)
    {
        return LiveRigError{
            LiveRigErrorCode::CouldNotCreateDirectory,
            "Could not create tone directory: " + error.message()
        };
    }

    return std::nullopt;
}

// Writes a UTF-8 text file after creating its parent directories.
[[nodiscard]] std::optional<LiveRigError> writeTextFile(
    const std::filesystem::path& path, const std::string& contents,
    LiveRigErrorCode write_error_code)
{
    if (const auto directory_error = createParentDirectory(path); directory_error.has_value())
    {
        return directory_error;
    }

    std::ofstream file{path, std::ios::binary};
    if (!file.is_open())
    {
        return LiveRigError{write_error_code, "Could not open tone file: " + path.string()};
    }

    file << contents;
    if (!file.good())
    {
        return LiveRigError{write_error_code, "Could not write tone file: " + path.string()};
    }

    return std::nullopt;
}

// Converts a plugin description into durable identity fields plus non-authoritative lookup hints.
[[nodiscard]] PluginIdentity makePluginIdentity(const juce::PluginDescription& description)
{
    const juce::String descriptive_name =
        description.descriptiveName.isNotEmpty() ? description.descriptiveName : description.name;
    return PluginIdentity{
        .format_name = description.pluginFormatName.toStdString(),
        .name = description.name.toStdString(),
        .descriptive_name = descriptive_name.toStdString(),
        .manufacturer = description.manufacturerName.toStdString(),
        .version = description.version.toStdString(),
        .unique_id = toHexString(description.uniqueId),
        .deprecated_uid = toHexString(description.deprecatedUid),
        .is_instrument = description.isInstrument,
        .original_file_or_identifier = description.fileOrIdentifier.toStdString(),
        .juce_identifier_hint = description.createIdentifierString().toStdString(),
        .tracktion_identifier_hint = tracktion::createIdentifierString(description).toStdString(),
    };
}

// Converts identity data back into a JUCE description shape for matching known plugins.
[[nodiscard]] juce::PluginDescription makePluginDescription(const PluginIdentity& identity)
{
    juce::PluginDescription description;
    description.pluginFormatName = juce::String::fromUTF8(identity.format_name.c_str());
    description.name = juce::String::fromUTF8(identity.name.c_str());
    description.descriptiveName = juce::String::fromUTF8(identity.descriptive_name.c_str());
    description.manufacturerName = juce::String::fromUTF8(identity.manufacturer.c_str());
    description.version = juce::String::fromUTF8(identity.version.c_str());
    description.uniqueId = fromHexString(identity.unique_id);
    description.deprecatedUid = fromHexString(identity.deprecated_uid);
    description.isInstrument = identity.is_instrument;
    description.fileOrIdentifier =
        juce::String::fromUTF8(identity.original_file_or_identifier.c_str());
    return description;
}

// Chooses stable display text for progress reports before Tracktion recreates the plugin.
[[nodiscard]] std::string pluginDisplayName(
    const PluginIdentity& identity, std::size_t plugin_index)
{
    if (!identity.descriptive_name.empty())
    {
        return identity.descriptive_name;
    }

    if (!identity.name.empty())
    {
        return identity.name;
    }

    return "Plugin " + std::to_string(plugin_index + 1);
}

// Sends progress only when the caller provided a progress callback.
void reportLiveRigLoadProgress(
    const LiveRigLoadRequest& request, std::size_t completed_plugins, std::size_t total_plugins,
    std::size_t active_plugin_index = 0, const std::string& active_plugin_name = {})
{
    if (!request.progress_callback)
    {
        return;
    }

    request.progress_callback(
        LiveRigLoadProgress{
            .completed_plugins = completed_plugins,
            .total_plugins = total_plugins,
            .active_plugin_index = active_plugin_index,
            .active_plugin_name = active_plugin_name,
        });
}

// Converts a Tracktion external plugin into editor-facing runtime chain state.
[[nodiscard]] LiveRigPlugin makeLiveRigPlugin(
    const tracktion::ExternalPlugin& plugin, std::size_t chain_index)
{
    return LiveRigPlugin{
        .instance_id = plugin.itemID.toString().toStdString(),
        .plugin_id = plugin.desc.createIdentifierString().toStdString(),
        .name = plugin.desc.name.toStdString(),
        .manufacturer = plugin.desc.manufacturerName.toStdString(),
        .format_name = plugin.desc.pluginFormatName.toStdString(),
        .chain_index = chain_index,
    };
}

// Serializes identity to the tone document's JSON shape.
[[nodiscard]] juce::var makeIdentityJson(const PluginIdentity& identity)
{
    return core::Json::makeObject({
        {"format", core::Json::makeString(identity.format_name)},
        {"name", core::Json::makeString(identity.name)},
        {"descriptiveName", core::Json::makeString(identity.descriptive_name)},
        {"manufacturer", core::Json::makeString(identity.manufacturer)},
        {"version", core::Json::makeString(identity.version)},
        {"uniqueId", core::Json::makeString(identity.unique_id)},
        {"deprecatedUid", core::Json::makeString(identity.deprecated_uid)},
        {"isInstrument", juce::var{identity.is_instrument}},
        {"originalFileOrIdentifier", core::Json::makeString(identity.original_file_or_identifier)},
        {"juceIdentifierHint", core::Json::makeString(identity.juce_identifier_hint)},
        {"tracktionIdentifierHint", core::Json::makeString(identity.tracktion_identifier_hint)},
    });
}

// Serializes the v1 tone document subset used by the current linear chain.
[[nodiscard]] juce::var makeToneDocumentJson(const ToneDocument& document)
{
    juce::var chain = core::Json::makeArray();
    for (const PluginRecord& plugin : document.chain)
    {
        chain.append(
            core::Json::makeObject({
                {"id", core::Json::makeString(plugin.id)},
                {"identity", makeIdentityJson(plugin.identity)},
                {"tracktionState", core::Json::makeString(plugin.tracktion_state_ref)},
            }));
    }

    juce::var slots = core::Json::makeArray();
    slots.append(
        core::Json::makeObject({
            {"id", core::Json::makeString("default")},
            {"name", core::Json::makeString("Default")},
            {"chain", chain},
            {"automation", core::Json::makeArray()},
        }));

    juce::var tone_clips = core::Json::makeArray();
    tone_clips.append(
        core::Json::makeObject({
            {"slot", core::Json::makeString("default")},
            {"startSeconds", juce::var{0.0}},
            {"endSeconds", juce::var{}},
        }));

    return core::Json::makeObject({
        {"formatVersion", juce::var{1}},
        {"slots", slots},
        {"toneClips", tone_clips},
    });
}

// Reads the identity object for one tone plugin record.
[[nodiscard]] PluginIdentity readPluginIdentity(const juce::var& object)
{
    return PluginIdentity{
        .format_name = core::Json::readOptionalString(object, "format"),
        .name = core::Json::readOptionalString(object, "name"),
        .descriptive_name = core::Json::readOptionalString(object, "descriptiveName"),
        .manufacturer = core::Json::readOptionalString(object, "manufacturer"),
        .version = core::Json::readOptionalString(object, "version"),
        .unique_id = core::Json::readOptionalString(object, "uniqueId"),
        .deprecated_uid = core::Json::readOptionalString(object, "deprecatedUid"),
        .is_instrument = core::Json::readOptionalBool(object, "isInstrument"),
        .original_file_or_identifier =
            core::Json::readOptionalString(object, "originalFileOrIdentifier"),
        .juce_identifier_hint = core::Json::readOptionalString(object, "juceIdentifierHint"),
        .tracktion_identifier_hint =
            core::Json::readOptionalString(object, "tracktionIdentifierHint"),
    };
}

// Reads the v1 tone document subset and validates all package-relative sidecar paths.
[[nodiscard]] std::expected<ToneDocument, LiveRigError> readToneDocument(
    const std::filesystem::path& song_directory, const std::string& tone_document_ref)
{
    if (!core::isCanonicalToneDocumentRef(tone_document_ref))
    {
        return std::unexpected{LiveRigError{
            LiveRigErrorCode::InvalidToneDocument,
            "Tone document path must be tones/<uuid>/tone.json: " + tone_document_ref
        }};
    }

    const auto tone_document_path = resolvePackageFile(song_directory, tone_document_ref);
    if (!tone_document_path.has_value())
    {
        return std::unexpected{LiveRigError{
            LiveRigErrorCode::MissingToneDocument,
            "Tone document is missing or unsafe: " + tone_document_ref
        }};
    }

    juce::FileInputStream tone_document_file{toJuceFile(*tone_document_path)};
    if (tone_document_file.failedToOpen())
    {
        return std::unexpected{LiveRigError{
            LiveRigErrorCode::CouldNotReadToneDocument,
            "Could not open tone document: " +
                tone_document_file.getStatus().getErrorMessage().toStdString()
        }};
    }

    auto parsed_document = core::Json::parseDocument(tone_document_file.readEntireStreamAsString());
    if (!parsed_document.has_value())
    {
        return std::unexpected{LiveRigError{
            LiveRigErrorCode::CouldNotReadToneDocument,
            "Could not parse tone document: " + parsed_document.error().message
        }};
    }

    const juce::var document_json = std::move(*parsed_document);
    if (!document_json.isObject() ||
        core::Json::readOptionalInt(document_json, "formatVersion", 0) != 1)
    {
        return std::unexpected{LiveRigError{
            LiveRigErrorCode::InvalidToneDocument, "Unsupported tone document formatVersion"
        }};
    }

    const juce::var& slots_json = core::Json::value(document_json, "slots");
    if (!slots_json.isArray() || slots_json.size() == 0)
    {
        return std::unexpected{LiveRigError{
            LiveRigErrorCode::InvalidToneDocument, "Tone document must contain at least one slot"
        }};
    }

    const juce::var& default_slot_json = slots_json[0];
    if (!default_slot_json.isObject())
    {
        return std::unexpected{LiveRigError{
            LiveRigErrorCode::InvalidToneDocument, "Tone document slot must be an object"
        }};
    }

    const juce::var& chain_json = core::Json::value(default_slot_json, "chain");
    if (!chain_json.isArray())
    {
        return std::unexpected{LiveRigError{
            LiveRigErrorCode::InvalidToneDocument, "Tone document slot chain must be an array"
        }};
    }

    const std::filesystem::path expected_state_directory =
        toneDocumentStateDirectory(std::filesystem::path{tone_document_ref});
    ToneDocument document;
    document.chain.reserve(static_cast<std::size_t>(chain_json.size()));
    for (int index = 0; index < chain_json.size(); ++index)
    {
        const juce::var& plugin_json = chain_json[index];
        if (!plugin_json.isObject())
        {
            return std::unexpected{LiveRigError{
                LiveRigErrorCode::InvalidToneDocument,
                "Tone document plugin entry must be an object"
            }};
        }

        const auto id = core::Json::readRequiredString(plugin_json, "id");
        const auto tracktion_state = core::Json::readRequiredString(plugin_json, "tracktionState");
        if (!id.has_value() || id->empty() || !tracktion_state.has_value() ||
            tracktion_state->empty())
        {
            return std::unexpected{LiveRigError{
                LiveRigErrorCode::InvalidToneDocument,
                "Tone document plugin entry is missing id or tracktionState"
            }};
        }

        if (!isCanonicalPluginStateRef(*tracktion_state, expected_state_directory))
        {
            return std::unexpected{LiveRigError{
                LiveRigErrorCode::InvalidToneDocument,
                "Tone plugin state must be under the tone state directory: " + *tracktion_state
            }};
        }

        if (!resolvePackageFile(song_directory, *tracktion_state).has_value())
        {
            return std::unexpected{LiveRigError{
                LiveRigErrorCode::MissingPluginState,
                "Tone plugin state is missing or unsafe: " + *tracktion_state
            }};
        }

        const juce::var& identity_json = core::Json::value(plugin_json, "identity");
        document.chain.push_back(
            PluginRecord{
                .id = *id,
                .identity =
                    identity_json.isObject() ? readPluginIdentity(identity_json) : PluginIdentity{},
                .tracktion_state_ref = *tracktion_state,
            });
    }

    return document;
}

// Writes the v1 tone document JSON file.
[[nodiscard]] std::optional<LiveRigError> writeToneDocument(
    const std::filesystem::path& tone_document_path, const ToneDocument& document)
{
    const juce::String json = juce::JSON::toString(makeToneDocumentJson(document));
    return writeTextFile(
        tone_document_path, json.toStdString() + '\n', LiveRigErrorCode::CouldNotWriteToneDocument);
}

// Reads a Tracktion plugin-state sidecar into a ValueTree.
[[nodiscard]] std::expected<juce::ValueTree, LiveRigError> readPluginStateTree(
    const std::filesystem::path& plugin_state_path)
{
    const std::unique_ptr<juce::XmlElement> xml = juce::parseXML(toJuceFile(plugin_state_path));
    if (xml == nullptr)
    {
        return std::unexpected{LiveRigError{
            LiveRigErrorCode::CouldNotReadPluginState,
            "Could not parse tone plugin state: " + plugin_state_path.string()
        }};
    }

    juce::ValueTree tree = juce::ValueTree::fromXml(*xml);
    if (!tree.isValid())
    {
        return std::unexpected{LiveRigError{
            LiveRigErrorCode::CouldNotReadPluginState,
            "Tone plugin state is not a valid ValueTree: " + plugin_state_path.string()
        }};
    }

    tree.removeProperty(tracktion::IDs::id, nullptr);
    return tree;
}

// Serializes a Tracktion plugin ValueTree exactly enough for Tracktion to recreate the plugin.
[[nodiscard]] std::expected<std::string, LiveRigError> makePluginStateXml(
    const juce::ValueTree& plugin_state, const std::filesystem::path& plugin_state_path)
{
    const std::unique_ptr<juce::XmlElement> xml = plugin_state.createXml();
    if (xml == nullptr)
    {
        return std::unexpected{LiveRigError{
            LiveRigErrorCode::CouldNotWritePluginState,
            "Could not serialize tone plugin state: " + plugin_state_path.string()
        }};
    }

    return xml->toString().toStdString();
}

// Builds the opaque project-owned candidate that UI and core callers can pass back to the host.
[[nodiscard]] PluginCandidate makePluginCandidate(
    const juce::PluginDescription& description, const std::filesystem::path& plugin_path)
{
    return PluginCandidate{
        .id = description.createIdentifierString().toStdString(),
        .name = description.name.toStdString(),
        .manufacturer = description.manufacturerName.toStdString(),
        .format_name = description.pluginFormatName.toStdString(),
        .file_path = plugin_path,
    };
}

// Converts the project-owned compact channel role into the Tracktion channel identifier.
[[nodiscard]] juce::AudioChannelSet::ChannelType toTracktionChannelRole(
    InstrumentChannelRole role) noexcept
{
    switch (role)
    {
        case InstrumentChannelRole::Left:
        {
            return juce::AudioChannelSet::left;
        }
        case InstrumentChannelRole::Right:
        {
            return juce::AudioChannelSet::right;
        }
    }

    return juce::AudioChannelSet::unknown;
}

// Converts the testable Rock Hero route description into Tracktion's wave-device type.
[[nodiscard]] tracktion::WaveDeviceDescription toTracktionWaveDeviceDescription(
    const InstrumentWaveDescription& description)
{
    std::vector<tracktion::ChannelIndex> channels;
    channels.reserve(description.channels.size());

    for (const InstrumentChannelDescription& channel : description.channels)
    {
        channels.emplace_back(channel.compact_device_channel, toTracktionChannelRole(channel.role));
    }

    return tracktion::WaveDeviceDescription{
        description.name, channels.data(), static_cast<int>(channels.size()), true
    };
}

// Describes the single instrument input and stereo output that Rock Hero exposes to Tracktion.
class RockHeroEngineBehaviour final : public tracktion::EngineBehaviour
{
public:
    // Lets Engine construct its edit before explicitly opening the device manager.
    bool autoInitialiseDeviceManager() override
    {
        return false;
    }

    // Scans third-party plugins in a child process so bad scans cannot wedge the editor process.
    bool canScanPluginsOutOfProcess() override
    {
        return true;
    }

    // Rock Hero supplies compact wave-device descriptions for the staged JUCE route.
    bool isDescriptionOfWaveDevicesSupported() override
    {
        return true;
    }

    // Converts the currently open JUCE device masks into Tracktion-visible wave devices.
    void describeWaveDevices(
        std::vector<tracktion::WaveDeviceDescription>& descriptions, juce::AudioIODevice& device,
        bool is_input) override
    {
        const std::optional<InstrumentWaveDeviceDescriptions> wave_descriptions =
            createTracktionInstrumentWaveDeviceDescriptions(
                device.getName(),
                device.getActiveInputChannels(),
                device.getActiveOutputChannels(),
                device.getInputChannelNames(),
                device.getOutputChannelNames());
        if (!wave_descriptions.has_value())
        {
            return;
        }

        descriptions.push_back(toTracktionWaveDeviceDescription(
            is_input ? wave_descriptions->input : wave_descriptions->output));
    }
};

// Top-level JUCE window that Tracktion owns through PluginWindowState::pluginWindow.
class PluginWindow final : public juce::DocumentWindow
{
public:
    // Creates a window only when Tracktion can supply a concrete plugin editor component.
    [[nodiscard]] static std::unique_ptr<juce::Component> create(tracktion::Plugin& plugin)
    {
        std::unique_ptr<tracktion::Plugin::EditorComponent> editor = plugin.createEditor();
        if (editor == nullptr)
        {
            return {};
        }

        return std::make_unique<PluginWindow>(plugin, std::move(editor));
    }

    // Takes ownership of Tracktion's editor component and lets plugin size changes drive bounds.
    PluginWindow(
        tracktion::Plugin& plugin, std::unique_ptr<tracktion::Plugin::EditorComponent> editor)
        : juce::DocumentWindow(
              plugin.getName(), juce::Colours::darkgrey,
              juce::DocumentWindow::closeButton | juce::DocumentWindow::minimiseButton)
        , m_plugin(plugin)
        , m_window_state(*plugin.windowState)
    {
        setUsingNativeTitleBar(true);

        // Configure the default ResizableWindow constrainer as a backstop. setEditor() will
        // replace this with the plugin editor's own constrainer when one is supplied; these
        // values only take effect for editors that allow resizing but don't provide a
        // constrainer of their own. The onscreen amounts also guard against restored bounds
        // landing on a monitor that no longer exists — the title bar (top) must stay fully
        // onscreen so the user can always drag the window back.
        getConstrainer()->setMinimumOnscreenAmounts(0x10000, 50, 30, 50);
        setResizeLimits(100, 50, 4000, 4000);

        setEditor(std::move(editor));

        // Restore the full saved rectangle on within-session reopen when the editor supports
        // resizing; otherwise fall back to the editor's natural size at Tracktion's chosen
        // position. Tracktion's own choosePositionForPluginWindow() returns only a Point, so
        // size would be lost across close/reopen without this branch. If an editor returns
        // empty bounds (signaling "host, pick a size"), the default ResizableWindow
        // constrainer's setResizeLimits floor of 100x50 takes over.
        const bool editor_allows_resizing = m_editor != nullptr && m_editor->allowWindowResizing();
        if (editor_allows_resizing && m_window_state.lastWindowBounds.has_value())
        {
            setBoundsConstrained(*m_window_state.lastWindowBounds);
        }
        else
        {
            setBoundsConstrained(getLocalBounds() + m_window_state.choosePositionForPluginWindow());
        }

        m_update_stored_bounds = true;
    }

    // Flushes any plugin state touched by the editor before Tracktion releases the window.
    ~PluginWindow() override
    {
        m_update_stored_bounds = false;
        m_plugin.edit.flushPluginStateIfNeeded(m_plugin);
        setEditor(nullptr);
    }

    // Recreates the plugin editor when Tracktion asks the host window to refresh its content.
    // setEditor() always clears existing state before installing the new editor, so no
    // separate setEditor(nullptr) call is needed first.
    void recreateEditor()
    {
        setEditor(m_plugin.createEditor());
    }

    // Standard Tracktion-host pattern (see external/tracktion_engine/examples/common/PluginWindow.h
    // and the default PluginWindowState::recreateWindowIfShowing): drop the current editor
    // synchronously, then recreate it on a short timer.
    //
    // Tracktion calls this hook from ExternalPlugin::forceFullReinitialise() right before it
    // tears down and replaces the underlying AudioPluginInstance. The current editor is bound to
    // the dying instance, so it must be released now; the replacement editor must be created
    // *after* forceFullReinitialise() finishes installing the new instance, which is why
    // creation is deferred onto the message loop. The "Async" suffix in
    // UIBehaviour::recreatePluginWindowContentAsync is contractual — Tracktion depends on this
    // being deferred.
    void recreateEditorAsync()
    {
        setEditor(nullptr);

        juce::Timer::callAfterDelay(
            50, [safe_this = juce::Component::SafePointer<PluginWindow>{this}] {
                if (auto* const window = safe_this.getComponent())
                {
                    window->recreateEditor();
                }
            });
    }

    // Routes the close button back through Tracktion so its window state stays authoritative.
    void closeButtonPressed() override
    {
        m_window_state.closeWindowExplicitly();
    }

    // Routes native system-close requests through the same Tracktion-owned close path.
    void userTriedToCloseWindow() override
    {
        closeButtonPressed();
    }

    // Plugin editors receive native scale notifications themselves; avoid double scaling the peer.
    [[nodiscard]] float getDesktopScaleFactor() const override
    {
        return 1.0F;
    }

    // Persists the latest bounds so reopening can restore the user's window position.
    void moved() override
    {
        storeWindowBounds();
    }

    // Persists resized bounds while leaving DocumentWindow to manage the content layout.
    void resized() override
    {
        juce::DocumentWindow::resized();
        storeWindowBounds();
    }

private:
    // Installs Tracktion's editor wrapper while preserving plugin-owned resize notifications.
    void setEditor(std::unique_ptr<tracktion::Plugin::EditorComponent> editor)
    {
        setConstrainer(nullptr);
        clearContentComponent();
        m_editor.reset();

        if (editor != nullptr)
        {
            m_editor = std::move(editor);
            setContentNonOwned(m_editor.get(), true);
        }

        const bool allow_window_resizing = m_editor == nullptr || m_editor->allowWindowResizing();
        setResizable(allow_window_resizing, false);

        if (m_editor != nullptr && allow_window_resizing)
        {
            setConstrainer(m_editor->getBoundsConstrainer());
        }
    }

    // Caches the latest user-driven bounds on Tracktion's window state so a closed-and-reopened
    // plugin window restores its last position within the session. Persistence across project
    // saves is handled at the project layer (see ProjectEditorState plumbing), so this path
    // intentionally does not call Edit::pluginChanged().
    void storeWindowBounds()
    {
        if (m_update_stored_bounds)
        {
            m_window_state.lastWindowBounds = getBounds();
        }
    }

    // Tracktion plugin whose editor is hosted by this window.
    tracktion::Plugin& m_plugin;

    // Tracktion owns this window and remains the source of truth for close/show state.
    tracktion::PluginWindowState& m_window_state;

    // Tracktion editor wrapper installed as the non-owned DocumentWindow content component.
    std::unique_ptr<tracktion::Plugin::EditorComponent> m_editor;

    // Prevents construction-time resized callbacks from overwriting Tracktion's default position.
    bool m_update_stored_bounds = false;
};

// Supplies Tracktion with Rock Hero's minimal plugin editor window implementation.
class RockHeroUIBehaviour final : public tracktion::UIBehaviour
{
public:
    // Creates windows only for normal plugin instances; rack windows will get their own UI later.
    std::unique_ptr<juce::Component> createPluginWindow(
        tracktion::PluginWindowState& window_state) override
    {
        auto* const plugin_window_state =
            dynamic_cast<tracktion::Plugin::WindowState*>(&window_state);
        if (plugin_window_state == nullptr)
        {
            return {};
        }

        return PluginWindow::create(plugin_window_state->plugin);
    }

    // Refreshes the editor contents without replacing Tracktion's owning plugin window.
    void recreatePluginWindowContentAsync(tracktion::Plugin& plugin) override
    {
        if (auto* const window =
                dynamic_cast<PluginWindow*>(plugin.windowState->pluginWindow.get()))
        {
            window->recreateEditorAsync();
            return;
        }

        tracktion::UIBehaviour::recreatePluginWindowContentAsync(plugin);
    }
};

// Opens an asset through Tracktion only long enough to validate it and read its duration.
[[nodiscard]] std::optional<common::core::TimeDuration> readAudioDuration(
    tracktion::Engine& engine, const common::core::AudioAsset& audio_asset)
{
    const juce::File file = toJuceFile(audio_asset.path);
    if (!file.existsAsFile())
    {
        return std::nullopt;
    }

    const tracktion::AudioFile audio_file(engine, file);
    if (!audio_file.isValid())
    {
        return std::nullopt;
    }

    const common::core::TimeDuration asset_duration{audio_file.getLength()};
    if (asset_duration.seconds <= 0.0)
    {
        return std::nullopt;
    }

    return asset_duration;
}

} // namespace

// Private Tracktion/JUCE adapter state hidden behind Engine's public pimpl boundary.
struct Engine::Impl : public juce::ChangeListener, public juce::ValueTree::Listener
{
private:
    friend class Engine;

    // Tracktion runtime root that owns device and plugin infrastructure.
    std::unique_ptr<tracktion::Engine> m_engine;

    // Two-track edit used for backing playback and instrument monitoring.
    std::unique_ptr<tracktion::Edit> m_edit;

    // Stable ID for the Tracktion track that owns arrangement backing clips.
    tracktion::EditItemID m_backing_track_id;

    // Stable ID for the Tracktion track that owns instrument input and future plugin FX.
    tracktion::EditItemID m_instrument_track_id;

    // Duration of the loaded audio, used to clamp seeks and detect end-of-file.
    double m_loaded_length_seconds{0.0};

    // Last coarse state used to suppress duplicate listener notifications.
    TransportState m_last_notified_transport_state{};

    // Message-thread listener list for the project-owned ITransport listener surface.
    juce::ListenerList<ITransport::Listener> m_transport_listeners;

    // Message-thread listener list for audio-device configuration changes.
    juce::ListenerList<IAudioDeviceConfiguration::Listener> m_audio_device_listeners;

    // Alive token captured by deferred MessageManager::callAsync lambdas so they can detect
    // Engine destruction before re-entering Impl state.
    std::shared_ptr<bool> m_alive{std::make_shared<bool>(true)};

    // In-flight async live rig load operation, when one is running. Reset between loads so the
    // result callback may safely start a new load.
    std::unique_ptr<LiveRigLoadOperation> m_load_op;

    // Starts the next plugin step: completes the load if all plugins are restored, otherwise
    // reports "Loading X" progress and yields before the heavy plugin construction.
    void beginNextPluginStep();

    // Performs the heavy plugin construction for the current step, reports "Loaded X" progress,
    // and yields before beginning the next plugin step.
    void executePluginStep();

    // Hands the supplied continuation to the request's yield callback when one is provided so a
    // paint cycle can run between cooperative steps; falls back to a plain async post otherwise.
    void yieldThenContinue(std::function<void()> next);

    // Aborts the in-flight live rig load with the supplied error, clearing the half-built chain
    // and rebuilding the instrument monitoring graph before invoking the completion callback.
    void abortLiveRigLoad(LiveRigError error);

    // Derives the current coarse transport state directly from Tracktion state.
    [[nodiscard]] TransportState currentTransportState() const noexcept
    {
        return TransportState{
            .playing = m_edit->getTransport().isPlaying(),
        };
    }

    // Creates the edit and gives its two audio tracks explicit product roles.
    void createEdit()
    {
        m_edit = tracktion::Edit::createSingleTrackEdit(*m_engine);
        auto audio_tracks = tracktion::getAudioTracks(*m_edit);
        tracktion::AudioTrack* const backing_track = audio_tracks.getFirst();

        if (backing_track != nullptr)
        {
            backing_track->setName("Backing");
            m_backing_track_id = backing_track->itemID;
        }
        else
        {
            logInstrumentMonitoringFailure("backing track was not created");
        }

        // Keep the authored signal chain empty until Tracktion built-ins are modeled in the
        // editor state and tone schema.
        constexpr bool add_default_plugins = false;
        const tracktion::AudioTrack::Ptr instrument_track = m_edit->insertNewAudioTrack(
            tracktion::TrackInsertPoint::getEndOfTracks(*m_edit), nullptr, add_default_plugins);

        if (instrument_track != nullptr)
        {
            instrument_track->setName("Instrument");
            m_instrument_track_id = instrument_track->itemID;
        }
        else
        {
            logInstrumentMonitoringFailure("instrument track was not created");
        }

        m_edit->playInStopEnabled = true;
    }

    // Derives coarse transport state from Tracktion and notifies listeners when it changes.
    void updateTransportState()
    {
        const TransportState current_state = currentTransportState();
        if (m_last_notified_transport_state == current_state)
        {
            return;
        }

        // Project-owned transport listeners observe only coarse transport state. Position is
        // intentionally excluded so view code polls it at render cadence without forcing callbacks
        // on every playhead tick.
        m_last_notified_transport_state = current_state;
        m_transport_listeners.call(
            &ITransport::Listener::onTransportStateChanged, m_last_notified_transport_state);
    }

    // Mirrors Tracktion transport and audio-device broadcasts into the project-owned surfaces.
    void changeListenerCallback(juce::ChangeBroadcaster* source) override
    {
        if (source == &m_engine->getDeviceManager().deviceManager)
        {
            applyInstrumentMonitoringRoute();
            m_audio_device_listeners.call(
                &IAudioDeviceConfiguration::Listener::onAudioDeviceConfigurationChanged);
            return;
        }
        updateTransportState();
    }

    // Tracktion publishes playhead movement through the transport ValueTree. The coarse state
    // surface ignores ordinary movement, but this hook still detects automatic end-of-file stops.
    void valueTreePropertyChanged(
        juce::ValueTree& /*tree*/, const juce::Identifier& property) override
    {
        if (property == tracktion::IDs::position && loadedAudioEndReached(currentBackendPosition()))
        {
            stopTransport();
        }
    }

    // Returns the timeline position the playback backend is currently producing, in seconds.
    //
    // During playback, the audible-timeline time trails the transport head by buffer latency and
    // best matches what leaves the output device. While stopped, instrument monitoring can keep a
    // Tracktion context allocated, so stopped reads must use the transport head instead of treating
    // the mere presence of a context as evidence of backing-track playback.
    [[nodiscard]] double currentBackendPosition() const
    {
        auto& transport = m_edit->getTransport();
        if (transport.isPlaying())
        {
            if (auto* const playback_context = transport.getCurrentPlaybackContext();
                playback_context != nullptr)
            {
                return playback_context->getAudibleTimelineTime().inSeconds();
            }
        }
        return transport.getPosition().inSeconds();
    }

    // Keeps externally requested positions inside the current loaded file duration.
    [[nodiscard]] double clampToLoadedRange(double seconds) const noexcept
    {
        if (m_loaded_length_seconds <= 0.0)
        {
            return std::max(0.0, seconds);
        }

        return std::clamp(seconds, 0.0, m_loaded_length_seconds);
    }

    // Returns the Tracktion audio track that owns backing arrangement clips.
    [[nodiscard]] tracktion::AudioTrack* backingTrack() const
    {
        return tracktion::findAudioTrackForID(*m_edit, m_backing_track_id);
    }

    // Returns the Tracktion audio track that receives the selected instrument input.
    [[nodiscard]] tracktion::AudioTrack* instrumentTrack() const
    {
        return tracktion::findAudioTrackForID(*m_edit, m_instrument_track_id);
    }

    // Looks up a previously scanned plugin candidate without exposing JUCE descriptions publicly.
    [[nodiscard]] std::unique_ptr<juce::PluginDescription> findKnownPlugin(
        const std::string& plugin_id) const
    {
        return m_engine->getPluginManager().knownPluginList.getTypeForIdentifierString(
            juce::String{plugin_id});
    }

    // Scans one plugin file through Tracktion's JUCE-backed known-plugin list. This is shared by
    // the worker-facing plugin-host port and message-thread live-rig restore; callers must keep it
    // off the realtime audio thread and avoid concurrent access to Tracktion's known-plugin list.
    [[nodiscard]] std::expected<std::vector<PluginCandidate>, PluginHostError>
    scanPluginFileForCandidates(const std::filesystem::path& plugin_path)
    {
        const juce::File plugin_file = toJuceFile(plugin_path);
        if (plugin_path.empty() || !plugin_file.exists())
        {
            return std::unexpected{PluginHostError{
                PluginHostErrorCode::MissingPluginFile,
                "Plugin file does not exist: " + plugin_path.string()
            }};
        }

        try
        {
            constexpr auto* vst3_format_name = "VST3";
            auto& plugin_manager = m_engine->getPluginManager();
            const juce::String& file_or_identifier = plugin_file.getFullPathName();
            juce::OwnedArray<juce::PluginDescription> found_descriptions;
            bool has_vst3_format = false;

            for (juce::AudioPluginFormat* const format :
                 plugin_manager.pluginFormatManager.getFormats())
            {
                if (format == nullptr || format->getName() != vst3_format_name)
                {
                    continue;
                }

                has_vst3_format = true;
                if (!format->fileMightContainThisPluginType(file_or_identifier))
                {
                    continue;
                }

                PluginScanTimeout scan_timeout{
                    [&plugin_manager] { plugin_manager.abortCurrentPluginScan(); },
                    g_plugin_scan_timeout
                };
                plugin_manager.knownPluginList.scanAndAddFile(
                    file_or_identifier, true, found_descriptions, *format);
                scan_timeout.finish();

                if (scan_timeout.timedOut())
                {
                    plugin_manager.knownPluginList.removeFromBlacklist(file_or_identifier);
                    plugin_manager.knownPluginList.scanFinished();
                    return std::unexpected{PluginHostError{
                        PluginHostErrorCode::PluginScanFailed,
                        "Plugin scan timed out after " +
                            std::to_string(g_plugin_scan_timeout.count()) +
                            " seconds: " + plugin_path.string()
                    }};
                }
            }

            if (!has_vst3_format)
            {
                return std::unexpected{PluginHostError{
                    PluginHostErrorCode::PluginScanFailed,
                    "VST3 plugin hosting is not enabled in this build"
                }};
            }

            plugin_manager.knownPluginList.scanFinished();

            std::vector<PluginCandidate> candidates;
            candidates.reserve(static_cast<std::size_t>(found_descriptions.size()));

            for (const juce::PluginDescription* description : found_descriptions)
            {
                if (description != nullptr && description->pluginFormatName == vst3_format_name)
                {
                    candidates.push_back(makePluginCandidate(*description, plugin_path));
                }
            }

            if (candidates.empty())
            {
                return std::unexpected{PluginHostError{
                    PluginHostErrorCode::NoCompatiblePlugin,
                    "No VST3 plugin was found in: " + plugin_path.string()
                }};
            }

            return candidates;
        }
        catch (const std::exception& error)
        {
            m_engine->getPluginManager().knownPluginList.scanFinished();
            return std::unexpected{PluginHostError{
                PluginHostErrorCode::PluginScanFailed,
                std::string{"Plugin scan failed: "} + error.what()
            }};
        }
    }

    // Appends a known plugin candidate to the instrument track.
    [[nodiscard]] std::expected<PluginHandle, PluginHostError> addKnownPluginToTrack(
        const std::string& plugin_id);

    // Checks the current known-plugin list for a plugin matching persisted identity.
    [[nodiscard]] bool hasKnownPluginForIdentity(const PluginIdentity& identity) const
    {
        const juce::PluginDescription persisted_description = makePluginDescription(identity);
        auto& known_plugin_list = m_engine->getPluginManager().knownPluginList;
        if (!identity.juce_identifier_hint.empty() &&
            known_plugin_list
                    .getTypeForIdentifierString(
                        juce::String::fromUTF8(identity.juce_identifier_hint.c_str()))
                    .get() != nullptr)
        {
            return true;
        }

        if (!identity.tracktion_identifier_hint.empty() &&
            known_plugin_list
                    .getTypeForIdentifierString(
                        juce::String::fromUTF8(identity.tracktion_identifier_hint.c_str()))
                    .get() != nullptr)
        {
            return true;
        }

        for (const juce::PluginDescription& known_description : known_plugin_list.getTypes())
        {
            if (persisted_description.isDuplicateOf(known_description))
            {
                return true;
            }
        }

        return false;
    }

    // Ensures a persisted plugin can be resolved before creating Tracktion state for it.
    [[nodiscard]] std::expected<void, LiveRigError> ensureKnownPluginForIdentity(
        const PluginIdentity& identity)
    {
        if (hasKnownPluginForIdentity(identity))
        {
            return {};
        }

        if (!identity.original_file_or_identifier.empty())
        {
            const std::filesystem::path plugin_path{identity.original_file_or_identifier};
            std::error_code error;
            if (std::filesystem::exists(plugin_path, error))
            {
                const auto scan_result = scanPluginFileForCandidates(plugin_path);
                if (!scan_result.has_value())
                {
                    return std::unexpected{LiveRigError{
                        LiveRigErrorCode::PluginScanFailed,
                        scan_result.error().message,
                    }};
                }

                if (hasKnownPluginForIdentity(identity))
                {
                    return {};
                }
            }
        }

        return std::unexpected{LiveRigError{
            LiveRigErrorCode::PluginNotFound, "Tone plugin was not found: " + identity.name
        }};
    }

    // Finds a loaded instrument-chain plugin by the opaque instance ID returned to callers.
    [[nodiscard]] tracktion::Plugin* findInstrumentPluginInstance(
        const std::string& instance_id) const
    {
        const tracktion::AudioTrack* const instrument_track = instrumentTrack();
        if (instrument_track == nullptr)
        {
            return nullptr;
        }

        const juce::String target_id{instance_id};
        // The returned plugin may be mutated by callers such as removePlugin().
        // NOLINTNEXTLINE(misc-const-correctness)
        for (tracktion::Plugin* const plugin : instrument_track->pluginList)
        {
            if (plugin != nullptr && plugin->itemID.toString() == target_id)
            {
                return plugin;
            }
        }

        return nullptr;
    }

    // Detects the moment Tracktion playback has reached or passed the loaded audio duration.
    [[nodiscard]] bool loadedAudioEndReached(double position_seconds) const
    {
        return m_loaded_length_seconds > 0.0 && m_edit->getTransport().isPlaying() &&
               position_seconds >= m_loaded_length_seconds;
    }

    // Tracktion's stop(false, false) halts playback but leaves the playhead where it is.
    void stopTracktionPlayback()
    {
        constexpr bool discard_recordings = false;
        constexpr bool clear_devices = false;
        m_edit->getTransport().stop(discard_recordings, clear_devices);
    }

    // Pauses Rock Hero playback without resetting the transport position.
    void pauseTransport()
    {
        stopTracktionPlayback();
    }

    // Stops Tracktion and tears down the active playback graph for graph mutation or shutdown.
    void stopTransportAndReleaseContext()
    {
        constexpr bool discard_recordings = false;
        constexpr bool clear_devices = true;
        auto& transport = m_edit->getTransport();
        transport.stop(discard_recordings, clear_devices);
        transport.freePlaybackContext();
    }

    // Removes instrument input assignments on the instrument track from the current playback
    // context.
    void clearInstrumentInputAssignments()
    {
        tracktion::AudioTrack* const instrument_track = instrumentTrack();
        if (instrument_track == nullptr)
        {
            return;
        }

        m_edit->getEditInputDevices().clearAllInputs(*instrument_track, nullptr);
    }

    // Finds the generated Tracktion wave input that corresponds to the selected JUCE mono input.
    [[nodiscard]] tracktion::WaveInputDevice* findInstrumentWaveInput(
        const InstrumentWaveDescription& description) const
    {
        const std::vector<tracktion::WaveInputDevice*> wave_inputs =
            m_engine->getDeviceManager().getWaveInputDevices();

        const auto matching_input = std::ranges::find_if(
            wave_inputs, [&description](const tracktion::WaveInputDevice* wave_input) {
                return wave_input != nullptr && wave_input->getName() == description.name;
            });

        if (matching_input == wave_inputs.end())
        {
            return nullptr;
        }

        return *matching_input;
    }

    // Clears any instrument route that can be reached through the current Tracktion playback
    // context.
    void detachInstrumentMonitoringRoute(const juce::String& reason)
    {
        logInstrumentMonitoringFailure(reason);

        m_engine->getDeviceManager().dispatchPendingUpdates();
        m_edit->getTransport().ensureContextAllocated(true);
        clearInstrumentInputAssignments();
        m_edit->getTransport().ensureContextAllocated(true);
    }

    // Binds the selected app-local mono input to the instrument Tracktion track.
    void applyInstrumentMonitoringRoute()
    {
        if (!juce::MessageManager::getInstance()->isThisTheMessageThread())
        {
            logInstrumentMonitoringFailure(
                "instrument route binding was requested off the message thread");
            return;
        }

        const tracktion::AudioTrack* const instrument_track = instrumentTrack();
        if (instrument_track == nullptr)
        {
            logInstrumentMonitoringFailure("instrument track is missing");
            return;
        }

        tracktion::DeviceManager& tracktion_device_manager = m_engine->getDeviceManager();
        juce::AudioIODevice* const current_device =
            tracktion_device_manager.deviceManager.getCurrentAudioDevice();
        if (current_device == nullptr)
        {
            detachInstrumentMonitoringRoute("no current audio device");
            return;
        }

        const std::optional<InstrumentWaveDeviceDescriptions> wave_descriptions =
            createTracktionInstrumentWaveDeviceDescriptions(
                current_device->getName(),
                current_device->getActiveInputChannels(),
                current_device->getActiveOutputChannels(),
                current_device->getInputChannelNames(),
                current_device->getOutputChannelNames());
        if (!wave_descriptions.has_value())
        {
            detachInstrumentMonitoringRoute(
                "selected route is not one mono input and one stereo output pair");
            return;
        }

        tracktion_device_manager.dispatchPendingUpdates();

        auto& transport = m_edit->getTransport();
        transport.ensureContextAllocated(true);
        clearInstrumentInputAssignments();

        tracktion::WaveInputDevice* const wave_input =
            findInstrumentWaveInput(wave_descriptions->input);
        if (wave_input == nullptr)
        {
            logInstrumentMonitoringFailure("selected mono input is not available to Tracktion");
            transport.ensureContextAllocated(true);
            return;
        }

        wave_input->setStereoPair(false);

        tracktion::InputDeviceInstance* const input_instance =
            m_edit->getCurrentInstanceForInputDevice(wave_input);
        if (input_instance == nullptr)
        {
            logInstrumentMonitoringFailure("selected mono input has no playback instance");
            transport.ensureContextAllocated(true);
            return;
        }

        const auto target_result = input_instance->setTarget(
            instrument_track->itemID, true, nullptr, std::optional<int>{0});
        if (!target_result)
        {
            logInstrumentMonitoringFailure(
                "could not assign instrument input to track: " + target_result.error());
            transport.ensureContextAllocated(true);
            return;
        }

        input_instance->setRecordingEnabled(instrument_track->itemID, false);
        wave_input->setMonitorMode(tracktion::InputDevice::MonitorMode::on);
        transport.ensureContextAllocated(true);
    }

    // Applies Rock Hero Stop-button semantics: halt playback and reset to timeline start.
    void stopTransport()
    {
        auto& transport = m_edit->getTransport();
        stopTracktionPlayback();
        transport.setPosition(tracktion::TimePosition{});
    }

    // Restores the instrument monitoring context after plugin-list graph mutation or failed
    // insertion.
    void rebuildInstrumentMonitoringGraph()
    {
        applyInstrumentMonitoringRoute();
        updateTransportState();
    }
};

// Handles the child-process entry point used by Tracktion's isolated plugin scanner.
bool Engine::startPluginScanChildProcess(std::string_view command_line)
{
    return tracktion::PluginManager::startChildProcessPluginScan(toJuceString(command_line));
}

// Checks whether a command line is addressed to Tracktion's isolated plugin scanner.
bool Engine::isPluginScanChildProcessCommandLine(std::string_view command_line)
{
    return toJuceString(command_line)
        .trim()
        .startsWith(toJuceString(g_plugin_scan_command_line_prefix));
}

// Creates the Tracktion engine and a minimal two-track edit for playback and instrument monitoring.
Engine::Engine()
    : m_impl(std::make_unique<Impl>())
{
    m_impl->m_engine = std::make_unique<tracktion::Engine>(
        "RockHero",
        std::make_unique<RockHeroUIBehaviour>(),
        std::make_unique<RockHeroEngineBehaviour>());
    m_impl->m_engine->getPluginManager().setUsesSeparateProcessForScanning(true);

    // createSingleTrackEdit already provides one AudioTrack ready for media.
    m_impl->createEdit();

    // Start with one instrument input and stereo output; the dialog can reconfigure either at
    // runtime.
    m_impl->m_engine->getDeviceManager().initialise(1, 2);
    m_impl->applyInstrumentMonitoringRoute();

    auto& device_manager = m_impl->m_engine->getDeviceManager().deviceManager;
    device_manager.addChangeListener(m_impl.get());

    // TransportControl derives from juce::ChangeBroadcaster and notifies on any transport
    // state change; Impl::changeListenerCallback filters to genuine play/pause transitions.
    m_impl->m_edit->getTransport().addChangeListener(m_impl.get());

    // Tracktion mirrors current playhead position into this public ValueTree property from its
    // transport loop. Listening here keeps the adapter event-driven from the UI perspective.
    m_impl->m_edit->getTransport().state.addListener(m_impl.get());

    // Seeds the project-owned state from the freshly created empty edit.
    m_impl->updateTransportState();
}

// Stops transport activity before destroying Tracktion objects in dependency order.
Engine::~Engine()
{
    if (m_impl->m_engine)
    {
        auto& device_manager = m_impl->m_engine->getDeviceManager().deviceManager;
        device_manager.removeChangeListener(m_impl.get());
    }

    if (m_impl->m_edit)
    {
        m_impl->m_edit->getTransport().state.removeListener(m_impl.get());
        m_impl->m_edit->getTransport().removeChangeListener(m_impl.get());
        m_impl->stopTransportAndReleaseContext();
    }

    m_impl->m_edit.reset();
    m_impl->m_engine.reset();
}

// Registers a project-owned transport listener that observes the message-thread snapshot.
void Engine::addListener(ITransport::Listener& listener)
{
    m_impl->m_transport_listeners.add(&listener);
}

// Removes a previously registered project-owned transport listener.
void Engine::removeListener(ITransport::Listener& listener)
{
    m_impl->m_transport_listeners.remove(&listener);
}

// Starts Tracktion transport playback from the current edit position.
void Engine::play()
{
    auto& transport = m_impl->m_edit->getTransport();
    if (m_impl->m_loaded_length_seconds > 0.0 &&
        transport.getPosition().inSeconds() >= m_impl->m_loaded_length_seconds)
    {
        transport.setPosition(tracktion::TimePosition{});
    }

    transport.play(false);
    m_impl->updateTransportState();
}

// Stops playback and resets Tracktion's transport position to the start.
void Engine::stop()
{
    m_impl->stopTransport();
    m_impl->updateTransportState();
}

// Pauses playback without resetting position so the user can resume from the same point.
void Engine::pause()
{
    m_impl->pauseTransport();
    m_impl->updateTransportState();
}

// Moves Tracktion transport to the requested timeline position. Position-only motion is observed
// through position(), not through the coarse state listener surface.
void Engine::seek(common::core::TimePosition position)
{
    const double clamped_seconds = m_impl->clampToLoadedRange(position.seconds);
    m_impl->m_edit->getTransport().setPosition(
        tracktion::TimePosition::fromSeconds(clamped_seconds));
}

// Returns the current project-owned state directly from the Tracktion adapter state.
TransportState Engine::state() const noexcept
{
    return m_impl->currentTransportState();
}

// Reads the timeline position for render-cadence cursor drawing. Delegates to the backend
// position helper, which uses audible time only while backing playback is running.
common::core::TimePosition Engine::position() const noexcept
{
    return common::core::TimePosition{m_impl->clampToLoadedRange(m_impl->currentBackendPosition())};
}

// Validates every arrangement audio file and records the accepted backend durations.
bool Engine::prepareSong(common::core::Song& song)
{
    for (common::core::Arrangement& arrangement : song.arrangements)
    {
        if (arrangement.audio_asset.path.empty())
        {
            return false;
        }

        const auto audio_duration = readAudioDuration(*m_impl->m_engine, arrangement.audio_asset);
        if (!audio_duration.has_value())
        {
            return false;
        }

        arrangement.audio_duration = *audio_duration;
    }

    return true;
}

// Makes the prepared arrangement active on the Tracktion backing audio track.
bool Engine::setActiveArrangement(const common::core::Arrangement& arrangement)
{
    auto* track = m_impl->backingTrack();
    if (track == nullptr)
    {
        return false;
    }

    if (arrangement.audio_asset.path.empty() || arrangement.audio_duration.seconds <= 0.0)
    {
        return false;
    }

    const juce::File file = toJuceFile(arrangement.audio_asset.path);
    if (!file.existsAsFile())
    {
        return false;
    }

    // Candidate is valid; stop playback and clear nodes before replacing Tracktion's edit graph.
    auto& transport = m_impl->m_edit->getTransport();
    m_impl->stopTransportAndReleaseContext();

    const auto start = tracktion::TimePosition{};
    const auto length = tracktion::TimeDuration::fromSeconds(arrangement.audio_duration.seconds);
    const tracktion::ClipPosition wave_clip_position{
        .time = {start, start + length}, .offset = tracktion::TimeDuration{}
    };

    // Final trailing argument asks Tracktion to replace any existing media on the track.
    const auto wave_clip =
        track->insertWaveClip(file.getFileNameWithoutExtension(), file, wave_clip_position, true);
    if (wave_clip == nullptr)
    {
        m_impl->applyInstrumentMonitoringRoute();
        m_impl->updateTransportState();
        return false;
    }

    m_impl->m_loaded_length_seconds = arrangement.audio_duration.seconds;
    transport.looping = false;
    transport.setPosition(tracktion::TimePosition{});
    m_impl->applyInstrumentMonitoringRoute();
    m_impl->updateTransportState();
    return true;
}

// Clears the backing track so closed projects do not leave stale media in Tracktion.
void Engine::clearActiveArrangement()
{
    auto& transport = m_impl->m_edit->getTransport();
    m_impl->stopTransportAndReleaseContext();
    transport.setPosition(tracktion::TimePosition{});

    if (auto* track = m_impl->backingTrack(); track != nullptr)
    {
        const juce::Array<tracktion::Clip*> clips = track->getClips();
        for (tracktion::Clip* clip : clips)
        {
            if (clip != nullptr)
            {
                clip->removeFromParent();
            }
        }
    }

    m_impl->m_loaded_length_seconds = 0.0;
    m_impl->applyInstrumentMonitoringRoute();
    m_impl->updateTransportState();
}

// Scans one plugin file through Tracktion's JUCE-backed known-plugin list. The editor invokes
// this from a worker thread so the busy overlay stays responsive while the file is inspected.
std::expected<std::vector<PluginCandidate>, PluginHostError> Engine::scanPluginFile(
    const std::filesystem::path& plugin_path)
{
    return m_impl->scanPluginFileForCandidates(plugin_path);
}

// Appends a selected known VST3 candidate to the instrument track's plugin list.
std::expected<PluginHandle, PluginHostError> Engine::Impl::addKnownPluginToTrack(
    const std::string& plugin_id)
{
    if (!juce::MessageManager::getInstance()->isThisTheMessageThread())
    {
        return std::unexpected{PluginHostError{PluginHostErrorCode::MessageThreadRequired}};
    }

    tracktion::AudioTrack* const instrument_track = instrumentTrack();
    if (instrument_track == nullptr)
    {
        return std::unexpected{PluginHostError{PluginHostErrorCode::TrackMissing}};
    }

    const std::unique_ptr<juce::PluginDescription> description = findKnownPlugin(plugin_id);
    if (description == nullptr)
    {
        return std::unexpected{PluginHostError{
            PluginHostErrorCode::PluginNotFound, "Plugin candidate was not found: " + plugin_id
        }};
    }

    if (!instrument_track->pluginList.canInsertPlugin())
    {
        return std::unexpected{PluginHostError{PluginHostErrorCode::PluginInsertionFailed}};
    }

    stopTransportAndReleaseContext();

    const tracktion::Plugin::Ptr plugin = m_edit->getPluginCache().createNewPlugin(
        tracktion::ExternalPlugin::xmlTypeName, *description);
    if (plugin == nullptr)
    {
        rebuildInstrumentMonitoringGraph();
        return std::unexpected{PluginHostError{
            PluginHostErrorCode::PluginCreationFailed,
            "Could not create plugin: " + description->name.toStdString()
        }};
    }

    if (auto* const external_plugin = dynamic_cast<tracktion::ExternalPlugin*>(plugin.get());
        external_plugin != nullptr)
    {
        const juce::String load_error = external_plugin->getLoadError();
        if (load_error.isNotEmpty())
        {
            rebuildInstrumentMonitoringGraph();
            return std::unexpected{
                PluginHostError{PluginHostErrorCode::PluginLoadFailed, load_error.toStdString()}
            };
        }
    }

    instrument_track->pluginList.insertPlugin(plugin, -1, nullptr);
    const int inserted_index = instrument_track->pluginList.indexOf(plugin.get());
    if (inserted_index < 0)
    {
        rebuildInstrumentMonitoringGraph();
        return std::unexpected{PluginHostError{PluginHostErrorCode::PluginInsertionFailed}};
    }

    rebuildInstrumentMonitoringGraph();
    return PluginHandle{
        .instance_id = plugin->itemID.toString().toStdString(),
        .plugin_id = plugin_id,
        .chain_index = static_cast<std::size_t>(inserted_index),
    };
}

// Appends a selected known VST3 candidate to the instrument track's plugin list.
std::expected<PluginHandle, PluginHostError> Engine::addPlugin(const std::string& plugin_id)
{
    return m_impl->addKnownPluginToTrack(plugin_id);
}

// Removes a loaded plugin from the instrument track and rebuilds monitoring around the mutation.
std::expected<void, PluginHostError> Engine::removePlugin(const std::string& instance_id)
{
    if (!juce::MessageManager::getInstance()->isThisTheMessageThread())
    {
        return std::unexpected{PluginHostError{PluginHostErrorCode::MessageThreadRequired}};
    }

    const tracktion::AudioTrack* const instrument_track = m_impl->instrumentTrack();
    if (instrument_track == nullptr)
    {
        return std::unexpected{PluginHostError{PluginHostErrorCode::TrackMissing}};
    }

    tracktion::Plugin* const plugin = m_impl->findInstrumentPluginInstance(instance_id);
    if (plugin == nullptr)
    {
        return std::unexpected{PluginHostError{
            PluginHostErrorCode::PluginInstanceNotFound,
            "Plugin instance was not found: " + instance_id
        }};
    }

    m_impl->stopTransportAndReleaseContext();
    plugin->deleteFromParent();
    m_impl->rebuildInstrumentMonitoringGraph();
    return {};
}

// Opens a plugin editor window through Tracktion's plugin window state.
std::expected<void, PluginHostError> Engine::openPluginWindow(const std::string& instance_id)
{
    if (!juce::MessageManager::getInstance()->isThisTheMessageThread())
    {
        return std::unexpected{PluginHostError{PluginHostErrorCode::MessageThreadRequired}};
    }

    if (m_impl->instrumentTrack() == nullptr)
    {
        return std::unexpected{PluginHostError{PluginHostErrorCode::TrackMissing}};
    }

    tracktion::Plugin* const plugin = m_impl->findInstrumentPluginInstance(instance_id);
    if (plugin == nullptr)
    {
        return std::unexpected{PluginHostError{
            PluginHostErrorCode::PluginInstanceNotFound,
            "Plugin instance was not found: " + instance_id,
        }};
    }

    plugin->showWindowExplicitly();

    // Safe to inspect synchronously: showWindowExplicitly() runs the entire
    // createPluginWindow -> setVisible chain on the message thread (gated above), so
    // isWindowShowing() is authoritative immediately after it returns. This catches real
    // failures such as plugins with no editor (createPluginWindow returns null) or
    // showWindow() bailing out because a modal dialog is in front.
    if (plugin->windowState == nullptr || !plugin->windowState->isWindowShowing())
    {
        return std::unexpected{PluginHostError{
            PluginHostErrorCode::PluginWindowUnavailable,
            "Plugin editor window could not be opened: " + plugin->getName().toStdString(),
        }};
    }

    return {};
}

// Clears the instrument plugin chain without touching the active backing arrangement.
std::expected<void, LiveRigError> Engine::clearRig()
{
    if (!juce::MessageManager::getInstance()->isThisTheMessageThread())
    {
        return std::unexpected{LiveRigError{LiveRigErrorCode::MessageThreadRequired}};
    }

    // Clear also cancels cooperative restore steps queued by loadRig(); otherwise stale
    // continuations could rebuild the chain after the editor has closed the project.
    m_impl->m_load_op.reset();

    tracktion::AudioTrack* const instrument_track = m_impl->instrumentTrack();
    if (instrument_track == nullptr)
    {
        return std::unexpected{LiveRigError{LiveRigErrorCode::TrackMissing}};
    }

    m_impl->stopTransportAndReleaseContext();
    instrument_track->pluginList.clear();
    m_impl->rebuildInstrumentMonitoringGraph();
    return {};
}

// Captures the current Tracktion live rig chain into a tone document plus plugin-state sidecars.
std::expected<LiveRigSnapshot, LiveRigError> Engine::captureActiveRig(
    const LiveRigCaptureRequest& request)
{
    if (!juce::MessageManager::getInstance()->isThisTheMessageThread())
    {
        return std::unexpected{LiveRigError{LiveRigErrorCode::MessageThreadRequired}};
    }

    if (request.song_directory.empty() || !core::isCanonicalPackageId(request.arrangement_id))
    {
        return std::unexpected{LiveRigError{LiveRigErrorCode::InvalidRequest}};
    }

    const std::filesystem::path tone_document_ref =
        request.existing_tone_document_ref.empty()
            ? generatedToneDocumentPath()
            : std::filesystem::path{request.existing_tone_document_ref};
    if (!isSafeRelativePath(tone_document_ref) ||
        !core::isCanonicalToneDocumentRef(tone_document_ref.generic_string()))
    {
        return std::unexpected{LiveRigError{
            LiveRigErrorCode::InvalidToneDocument,
            "Tone document path must be tones/<uuid>/tone.json: " +
                tone_document_ref.generic_string()
        }};
    }

    const tracktion::AudioTrack* const instrument_track = m_impl->instrumentTrack();
    if (instrument_track == nullptr)
    {
        return std::unexpected{LiveRigError{LiveRigErrorCode::TrackMissing}};
    }

    m_impl->stopTransportAndReleaseContext();

    ToneDocument document;
    LiveRigSnapshot snapshot{
        .tone_document_ref = tone_document_ref.generic_string(),
        .plugins = {},
    };
    const tracktion::Plugin::Array plugins = instrument_track->pluginList.getPlugins();
    document.chain.reserve(static_cast<std::size_t>(plugins.size()));
    snapshot.plugins.reserve(static_cast<std::size_t>(plugins.size()));
    const std::filesystem::path plugin_state_directory =
        toneDocumentStateDirectory(tone_document_ref);

    std::size_t captured_plugin_index = 0;
    for (tracktion::Plugin* const plugin : plugins)
    {
        if (plugin == nullptr)
        {
            continue;
        }

        auto* const external_plugin = dynamic_cast<tracktion::ExternalPlugin*>(plugin);
        if (external_plugin == nullptr)
        {
            // TODO: Remove this blocker once supported Tracktion built-ins are modeled by
            // `000-tracktion-built-in-controls-plan.md`; until then, failing is safer than
            // silently dropping tone content.
            m_impl->rebuildInstrumentMonitoringGraph();
            return std::unexpected{LiveRigError{
                LiveRigErrorCode::UnsupportedPlugin,
                "Only external plugins can be captured right now: " +
                    plugin->getName().toStdString()
            }};
        }

        const std::size_t chain_index = captured_plugin_index;
        external_plugin->flushPluginStateToValueTree();
        juce::ValueTree plugin_state = external_plugin->state.createCopy();
        plugin_state.removeProperty(tracktion::IDs::id, nullptr);

        const std::filesystem::path plugin_state_ref =
            generatedPluginStatePath(plugin_state_directory, chain_index);
        const std::filesystem::path plugin_state_path = request.song_directory / plugin_state_ref;
        const auto plugin_state_xml = makePluginStateXml(plugin_state, plugin_state_path);
        if (!plugin_state_xml.has_value())
        {
            m_impl->rebuildInstrumentMonitoringGraph();
            return std::unexpected{plugin_state_xml.error()};
        }

        if (const auto write_error = writeTextFile(
                plugin_state_path, *plugin_state_xml, LiveRigErrorCode::CouldNotWritePluginState);
            write_error.has_value())
        {
            m_impl->rebuildInstrumentMonitoringGraph();
            return std::unexpected{*write_error};
        }

        document.chain.push_back(
            PluginRecord{
                .id = "plugin-" + std::to_string(chain_index + 1),
                .identity = makePluginIdentity(external_plugin->desc),
                .tracktion_state_ref = plugin_state_ref.generic_string(),
            });
        snapshot.plugins.push_back(makeLiveRigPlugin(*external_plugin, chain_index));
        ++captured_plugin_index;
    }

    const std::filesystem::path tone_document_path = request.song_directory / tone_document_ref;
    if (const auto write_error = writeToneDocument(tone_document_path, document);
        write_error.has_value())
    {
        m_impl->rebuildInstrumentMonitoringGraph();
        return std::unexpected{*write_error};
    }

    m_impl->rebuildInstrumentMonitoringGraph();
    return snapshot;
}

// Kicks off the cooperative async live rig load: validates the request, reads the tone document
// up front, clears the existing chain, and posts the first plugin step on the message loop so
// the busy overlay has a chance to paint before plugin construction starts.
void Engine::loadRig(LiveRigLoadRequest request, LiveRigLoadResultCallback on_result)
{
    if (!on_result)
    {
        return;
    }

    if (!juce::MessageManager::getInstance()->isThisTheMessageThread())
    {
        on_result(std::unexpected{LiveRigError{LiveRigErrorCode::MessageThreadRequired}});
        return;
    }

    if (request.tone_document_ref.empty())
    {
        auto clear_result = clearRig();
        if (!clear_result.has_value())
        {
            on_result(std::unexpected{std::move(clear_result.error())});
            return;
        }
        on_result(LiveRigLoadResult{});
        return;
    }

    if (request.song_directory.empty())
    {
        on_result(std::unexpected{LiveRigError{LiveRigErrorCode::InvalidRequest}});
        return;
    }

    auto document = readToneDocument(request.song_directory, request.tone_document_ref);
    if (!document.has_value())
    {
        on_result(std::unexpected{std::move(document.error())});
        return;
    }

    tracktion::AudioTrack* const instrument_track = m_impl->instrumentTrack();
    if (instrument_track == nullptr)
    {
        on_result(std::unexpected{LiveRigError{LiveRigErrorCode::TrackMissing}});
        return;
    }

    m_impl->stopTransportAndReleaseContext();
    instrument_track->pluginList.clear();

    auto operation = std::make_unique<LiveRigLoadOperation>();
    operation->request = std::move(request);
    operation->chain = std::move(document->chain);
    operation->display_names.reserve(operation->chain.size());
    for (std::size_t plugin_index = 0; plugin_index < operation->chain.size(); ++plugin_index)
    {
        operation->display_names.push_back(
            pluginDisplayName(operation->chain[plugin_index].identity, plugin_index));
    }
    operation->result.plugins.reserve(operation->chain.size());
    operation->on_result = std::move(on_result);

    reportLiveRigLoadProgress(operation->request, 0, operation->chain.size());

    m_impl->m_load_op = std::move(operation);

    // Yield through the caller-provided paint fence so the initial "Loading live rig..." state
    // actually paints before plugin construction starts.
    std::weak_ptr<bool> load_alive_source = m_impl->m_alive;
    m_impl->yieldThenContinue([this, load_alive = std::move(load_alive_source)] {
        if (load_alive.expired())
        {
            return;
        }
        m_impl->beginNextPluginStep();
    });
}

// Starts the next plugin's step: completes the load if no plugins remain, otherwise reports
// "Loading X" progress and yields so the busy overlay actually paints the new state before the
// heavy plugin construction blocks the message thread.
void Engine::Impl::beginNextPluginStep()
{
    if (m_load_op == nullptr)
    {
        return;
    }

    const std::size_t total_plugins = m_load_op->chain.size();
    if (m_load_op->next_index >= total_plugins)
    {
        auto operation = std::move(m_load_op);
        rebuildInstrumentMonitoringGraph();
        operation->on_result(std::move(operation->result));
        return;
    }

    const std::size_t plugin_index = m_load_op->next_index;
    const std::string& display_name = m_load_op->display_names[plugin_index];

    // "Loading X" before the heavy work so the bar updates to the new plugin name immediately.
    reportLiveRigLoadProgress(
        m_load_op->request, plugin_index, total_plugins, plugin_index, display_name);

    std::weak_ptr<bool> load_alive_source = m_alive;
    yieldThenContinue([this, load_alive = std::move(load_alive_source)] {
        if (load_alive.expired())
        {
            return;
        }
        executePluginStep();
    });
}

// Performs the heavy work for the current plugin (scan-if-needed, read state, insert,
// error-check), reports "Loaded X" progress, then yields again so the completion of that plugin
// is visible before the next plugin's "Loading" message replaces it.
void Engine::Impl::executePluginStep()
{
    if (m_load_op == nullptr)
    {
        return;
    }

    const std::size_t plugin_index = m_load_op->next_index;
    const std::size_t total_plugins = m_load_op->chain.size();
    const PluginRecord& plugin = m_load_op->chain[plugin_index];
    const std::string& display_name = m_load_op->display_names[plugin_index];

    auto plugin_known = ensureKnownPluginForIdentity(plugin.identity);
    if (!plugin_known.has_value())
    {
        abortLiveRigLoad(std::move(plugin_known.error()));
        return;
    }

    const auto plugin_state_path =
        resolvePackageFile(m_load_op->request.song_directory, plugin.tracktion_state_ref);
    if (!plugin_state_path.has_value())
    {
        abortLiveRigLoad(
            LiveRigError{
                LiveRigErrorCode::MissingPluginState,
                "Tone plugin state is missing or unsafe: " + plugin.tracktion_state_ref
            });
        return;
    }

    auto plugin_state = readPluginStateTree(*plugin_state_path);
    if (!plugin_state.has_value())
    {
        abortLiveRigLoad(std::move(plugin_state.error()));
        return;
    }

    tracktion::AudioTrack* const instrument_track = instrumentTrack();
    if (instrument_track == nullptr)
    {
        abortLiveRigLoad(LiveRigError{LiveRigErrorCode::TrackMissing});
        return;
    }

    const tracktion::Plugin::Ptr inserted_plugin =
        instrument_track->pluginList.insertPlugin(*plugin_state, -1);
    auto* const external_plugin =
        inserted_plugin != nullptr ? dynamic_cast<tracktion::ExternalPlugin*>(inserted_plugin.get())
                                   : nullptr;
    if (external_plugin == nullptr)
    {
        abortLiveRigLoad(
            LiveRigError{
                LiveRigErrorCode::PluginRestoreFailed, "Could not insert persisted tone plugin"
            });
        return;
    }

    const juce::String load_error = external_plugin->getLoadError();
    if (load_error.isNotEmpty())
    {
        abortLiveRigLoad(
            LiveRigError{LiveRigErrorCode::PluginRestoreFailed, load_error.toStdString()});
        return;
    }

    m_load_op->result.plugins.push_back(
        makeLiveRigPlugin(*external_plugin, m_load_op->result.plugins.size()));
    m_load_op->next_index = plugin_index + 1;

    // "Loaded X" advances the bar to N+1/T so the user sees the per-plugin completion the spec
    // calls for, and so a one-plugin chain hits 100% before the overlay clears.
    reportLiveRigLoadProgress(
        m_load_op->request,
        m_load_op->result.plugins.size(),
        total_plugins,
        plugin_index,
        display_name);

    std::weak_ptr<bool> load_alive_source = m_alive;
    yieldThenContinue([this, load_alive = std::move(load_alive_source)] {
        if (load_alive.expired())
        {
            return;
        }
        beginNextPluginStep();
    });
}

// Routes the continuation through the caller's yield callback when one is provided so each step
// waits for a real paint cycle before resuming. Falls back to plain callAsync so the loop still
// advances when the caller has not supplied a paint fence (e.g. headless tests).
void Engine::Impl::yieldThenContinue(std::function<void()> next)
{
    if (!next)
    {
        return;
    }

    if (m_load_op != nullptr && m_load_op->request.yield_callback)
    {
        m_load_op->request.yield_callback(std::move(next));
        return;
    }

    juce::MessageManager::callAsync(std::move(next));
}

// Tears down a partially loaded chain and delivers the failure to the original caller.
void Engine::Impl::abortLiveRigLoad(LiveRigError error)
{
    if (m_load_op == nullptr)
    {
        return;
    }

    auto operation = std::move(m_load_op);
    if (tracktion::AudioTrack* const instrument_track = instrumentTrack();
        instrument_track != nullptr)
    {
        instrument_track->pluginList.clear();
    }
    rebuildInstrumentMonitoringGraph();
    operation->on_result(std::unexpected{std::move(error)});
}

// Exposes the JUCE device manager so settings UI can host the stock device selector directly.
juce::AudioDeviceManager& Engine::deviceManager() noexcept
{
    return m_impl->m_engine->getDeviceManager().deviceManager;
}

// Returns the currently open device name through the JUCE device manager.
std::optional<std::string> Engine::currentDeviceName() const
{
    const auto* const current_device =
        m_impl->m_engine->getDeviceManager().deviceManager.getCurrentAudioDevice();
    if (current_device == nullptr)
    {
        return std::nullopt;
    }

    const juce::String name = current_device->getName();
    if (name.isEmpty())
    {
        return std::nullopt;
    }

    return name.toStdString();
}

// Registers a project-owned device-configuration listener.
void Engine::addListener(IAudioDeviceConfiguration::Listener& listener)
{
    m_impl->m_audio_device_listeners.add(&listener);
}

// Removes a previously registered device-configuration listener.
void Engine::removeListener(IAudioDeviceConfiguration::Listener& listener)
{
    m_impl->m_audio_device_listeners.remove(&listener);
}

// Creates an IThumbnail wrapper without exposing Tracktion types through public UI-facing headers.
std::unique_ptr<IThumbnail> Engine::createThumbnail(juce::Component& owner)
{
    return std::make_unique<TracktionThumbnail>(*m_impl->m_engine, owner);
}

} // namespace rock_hero::common::audio
