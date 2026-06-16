#include "engine.h"

#include "live_rig_gain_plugin.h"
#include "monitoring_mode_transition.h"
#include "plugin_move_index.h"
#include "tracktion_instrument_wave_device_mapping.h"
#include "tracktion_thumbnail.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstddef>
#include <exception>
#include <expected>
#include <filesystem>
#include <fstream>
#include <functional>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <rock_hero/common/audio/plugin_chain_limits.h>
#include <rock_hero/common/core/application_identity.h>
#include <rock_hero/common/core/json.h>
#include <rock_hero/common/core/juce_path.h>
#include <rock_hero/common/core/logger.h>
#include <rock_hero/common/core/package_id.h>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <tracktion_engine/tracktion_engine.h>
#include <unordered_set>
#include <utility>
#include <vector>

#if JUCE_WINDOWS
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace rock_hero::common::audio
{

namespace
{

constexpr std::string_view g_tone_state_directory_name{"state"};
constexpr std::string_view g_plugin_state_extension{".tracktion-plugin"};
constexpr std::string_view g_plugin_scan_command_line_prefix{"--PluginScan:"};
constexpr auto g_plugin_scan_timeout = std::chrono::seconds{30};

enum class PluginWindowCommand
{
    Undo,
    Redo,
};

using PluginWindowCommandDispatcher = std::function<void(PluginWindowCommand)>;

[[nodiscard]] int normalizedAsciiKeyCode(int key_code) noexcept
{
    if (key_code >= 'A' && key_code <= 'Z')
    {
        return key_code - 'A' + 'a';
    }
    return key_code;
}

[[nodiscard]] bool hasCommandShortcutModifier(const juce::KeyPress& key) noexcept
{
    const juce::ModifierKeys modifiers = key.getModifiers();
    return modifiers.isCommandDown() && !modifiers.isAltDown();
}

[[nodiscard]] bool isUndoShortcut(const juce::KeyPress& key) noexcept
{
    return hasCommandShortcutModifier(key) && !key.getModifiers().isShiftDown() &&
           normalizedAsciiKeyCode(key.getKeyCode()) == 'z';
}

[[nodiscard]] bool isRedoShortcut(const juce::KeyPress& key) noexcept
{
    return hasCommandShortcutModifier(key) && !key.getModifiers().isShiftDown() &&
           normalizedAsciiKeyCode(key.getKeyCode()) == 'y';
}

// Formats filesystem paths as UTF-8 text for stable IDs and logs. path::string() can lossy-convert
// through the active code page on Windows and drop non-ANSI characters from plugin paths.
[[nodiscard]] std::string pathToUtf8String(const std::filesystem::path& path)
{
    const std::u8string encoded = path.u8string();
    std::string result;
    result.reserve(encoded.size());
    for (const char8_t byte : encoded)
    {
        result.push_back(static_cast<char>(byte));
    }

    return result;
}

[[nodiscard]] std::string normalizedPathKey(const std::filesystem::path& path)
{
    std::string key = pathToUtf8String(path.lexically_normal());
#if defined(_WIN32)
    std::ranges::transform(key, key.begin(), [](const unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
#endif
    return key;
}

[[nodiscard]] bool hasVst3Extension(const std::filesystem::path& path)
{
    std::string extension = path.extension().string();
    std::ranges::transform(extension, extension.begin(), [](const unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return extension == ".vst3";
}

// JUCE may persist a Windows VST3 either as the bundle directory or as the architecture-specific
// module inside Contents. Normalize both forms to the bundle for UI display and path deduping.
[[nodiscard]] std::filesystem::path vst3DisplayPath(const std::filesystem::path& path)
{
#if defined(_WIN32)
    const std::filesystem::path architecture_path = path.parent_path();
    const std::filesystem::path contents_path = architecture_path.parent_path();
    const std::filesystem::path bundle_path = contents_path.parent_path();
    if (contents_path.filename() == "Contents" && hasVst3Extension(bundle_path))
    {
        return bundle_path;
    }
#endif
    return path;
}

[[nodiscard]] std::string normalizedPluginPathKey(const std::filesystem::path& path)
{
    return normalizedPathKey(vst3DisplayPath(path));
}

// Converts JUCE sample-count latency to the millisecond value shown in editor status text.
[[nodiscard]] double samplesToMilliseconds(int sample_count, double sample_rate_hz) noexcept
{
    if (sample_count <= 0 || sample_rate_hz <= 0.0)
    {
        return 0.0;
    }

    return static_cast<double>(sample_count) * 1000.0 / sample_rate_hz;
}

[[nodiscard]] std::chrono::milliseconds elapsedMilliseconds(
    const std::chrono::steady_clock::time_point started_at)
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - started_at);
}

void logPluginCatalogScanSummary(
    const std::size_t candidate_paths, const std::chrono::milliseconds total_duration)
{
    RH_LOG_INFO(
        "audio.plugin_catalog_scan",
        "Plugin catalog scan completed candidate_paths={} duration_ms={}",
        candidate_paths,
        total_duration.count());
}

void reportPluginCatalogScanProgress(
    const PluginCatalogScanProgressCallback& progress_callback, std::size_t completed_plugins,
    std::size_t total_plugins, const std::filesystem::path& active_plugin_path)
{
    if (!progress_callback)
    {
        return;
    }

    progress_callback(
        PluginCatalogScanProgress{
            .completed_plugins = std::min(completed_plugins, total_plugins),
            .total_plugins = total_plugins,
            .active_plugin_path = active_plugin_path,
        });
}

void logPluginValidationSummary(
    const std::filesystem::path& plugin_path, const std::chrono::milliseconds total_duration,
    const std::optional<std::string>& failure_message)
{
    const std::string plugin_path_text = pathToUtf8String(plugin_path);

    if (failure_message.has_value() && !failure_message->empty())
    {
        RH_LOG_WARNING(
            "audio.plugin_validation",
            "Plugin validation failed plugin_path={} duration_ms={} error={}",
            plugin_path_text,
            total_duration.count(),
            *failure_message);
    }
    else
    {
        RH_LOG_INFO(
            "audio.plugin_validation",
            "Plugin validation succeeded plugin_path={} duration_ms={}",
            plugin_path_text,
            total_duration.count());
    }
}

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

    // Opaque editor-owned visual block carried through the tone document. The audio layer never
    // interprets it (no gap rules, no validation); playback ignores it entirely. The editor owns
    // its meaning and validity.
    std::size_t block_index{};

    // Opaque editor-owned display type override token carried through the tone document. Empty
    // means the editor should use its automatic display classification.
    std::string display_type_override;
};

// V1 tone document subset currently used by the linear plugin-chain runtime.
struct ToneDocument
{
    std::vector<PluginRecord> chain;
    Gain output_gain;
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

    // Output gain from the parsed tone document, applied after all external plugins are restored.
    Gain output_gain;
};

struct PluginChainMutationFailure
{
    PluginHostError error;
    std::string_view reroute_context;
};

// Records recoverable instrument-route failures without turning an internal bind into a public
// error.
void logInstrumentMonitoringFailure(const juce::String& message)
{
    RH_LOG_WARNING(
        "audio.instrument_monitoring",
        "Instrument monitoring route failed detail={}",
        message.toStdString());
}

// Converts a route-bind failure into the live-input error surface used by calibration setup.
[[nodiscard]] LiveInputError liveInputRouteUnavailable(const juce::String& message)
{
    logInstrumentMonitoringFailure(message);
    return LiveInputError{
        LiveInputErrorCode::InputRouteUnavailable,
        ("Live input route is unavailable: " + message).toStdString(),
    };
}

// Maps structural live-rig failures into the narrower live-input setup surface.
[[nodiscard]] LiveInputError liveInputErrorFromLiveRigError(const LiveRigError& error)
{
    switch (error.code)
    {
        case LiveRigErrorCode::MessageThreadRequired:
        {
            return LiveInputError{LiveInputErrorCode::MessageThreadRequired, error.message};
        }
        case LiveRigErrorCode::TrackMissing:
        {
            return LiveInputError{LiveInputErrorCode::TrackMissing, error.message};
        }
        default:
        {
            return LiveInputError{LiveInputErrorCode::CouldNotSetInputGain, error.message};
        }
    }
}

// Maps monitoring rebuild failures into plugin-host mutation errors.
[[nodiscard]] PluginHostError pluginHostErrorFromLiveInputError(const LiveInputError& error)
{
    switch (error.code)
    {
        case LiveInputErrorCode::MessageThreadRequired:
        {
            return PluginHostError{PluginHostErrorCode::MessageThreadRequired, error.message};
        }
        case LiveInputErrorCode::TrackMissing:
        {
            return PluginHostError{PluginHostErrorCode::TrackMissing, error.message};
        }
        default:
        {
            return PluginHostError{PluginHostErrorCode::MonitoringRouteFailed, error.message};
        }
    }
}

// Maps monitoring rebuild failures into live-rig mutation errors.
[[nodiscard]] LiveRigError liveRigErrorFromLiveInputError(const LiveInputError& error)
{
    switch (error.code)
    {
        case LiveInputErrorCode::MessageThreadRequired:
        {
            return LiveRigError{LiveRigErrorCode::MessageThreadRequired, error.message};
        }
        case LiveInputErrorCode::TrackMissing:
        {
            return LiveRigError{LiveRigErrorCode::TrackMissing, error.message};
        }
        default:
        {
            return LiveRigError{LiveRigErrorCode::MonitoringRouteFailed, error.message};
        }
    }
}

// Maps monitoring rebuild failures into song-audio activation errors.
[[nodiscard]] SongAudioError songAudioErrorFromLiveInputError(const LiveInputError& error)
{
    return SongAudioError{SongAudioErrorCode::MonitoringRouteFailed, error.message};
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
[[nodiscard]] std::expected<void, LiveRigError> createParentDirectory(
    const std::filesystem::path& output_file)
{
    std::error_code error;
    std::filesystem::create_directories(output_file.parent_path(), error);
    if (error)
    {
        return std::unexpected{LiveRigError{
            LiveRigErrorCode::CouldNotCreateDirectory,
            "Could not create tone directory: " + error.message()
        }};
    }

    return {};
}

// Writes a UTF-8 text file after creating its parent directories.
[[nodiscard]] std::expected<void, LiveRigError> writeTextFile(
    const std::filesystem::path& path, const std::string& contents,
    LiveRigErrorCode write_error_code)
{
    if (auto directory_result = createParentDirectory(path); !directory_result.has_value())
    {
        return std::unexpected{std::move(directory_result.error())};
    }

    std::ofstream file{path, std::ios::binary};
    if (!file.is_open())
    {
        return std::unexpected{
            LiveRigError{write_error_code, "Could not open tone file: " + path.string()}
        };
    }

    file << contents;
    if (!file.good())
    {
        return std::unexpected{
            LiveRigError{write_error_code, "Could not write tone file: " + path.string()}
        };
    }

    return {};
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

// Converts a Tracktion plugin into editor-facing runtime chain state.
[[nodiscard]] PluginChainEntry makePluginChainEntry(
    const tracktion::Plugin& plugin, std::size_t chain_index)
{
    if (const auto* const external_plugin = dynamic_cast<const tracktion::ExternalPlugin*>(&plugin))
    {
        return PluginChainEntry{
            .instance_id = external_plugin->itemID.toString().toStdString(),
            .plugin_id = external_plugin->desc.createIdentifierString().toStdString(),
            .name = external_plugin->desc.name.toStdString(),
            .manufacturer = external_plugin->desc.manufacturerName.toStdString(),
            .format_name = external_plugin->desc.pluginFormatName.toStdString(),
            .category = external_plugin->desc.category.toStdString(),
            .chain_index = chain_index,
        };
    }

    // Non-external plugins expose no plugin descriptor, so there is no stable plugin identifier to
    // report. Leave plugin_id empty rather than duplicating the instance ID, which would falsely
    // present a per-instance ID as a reusable plugin identity.
    return PluginChainEntry{
        .instance_id = plugin.itemID.toString().toStdString(),
        .plugin_id = {},
        .name = plugin.getName().toStdString(),
        .manufacturer = {},
        .format_name = {},
        .category = {},
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
        juce::var plugin_json = core::Json::makeObject({
            {"id", core::Json::makeString(plugin.id)},
            {"identity", makeIdentityJson(plugin.identity)},
            {"tracktionState", core::Json::makeString(plugin.tracktion_state_ref)},
            {"blockIndex", juce::var{static_cast<int>(plugin.block_index)}},
        });
        if (!plugin.display_type_override.empty())
        {
            plugin_json.getDynamicObject()->setProperty(
                "displayTypeOverride", core::Json::makeString(plugin.display_type_override));
        }

        chain.append(plugin_json);
    }

    juce::var slots = core::Json::makeArray();
    slots.append(
        core::Json::makeObject({
            {"id", core::Json::makeString("default")},
            {"name", core::Json::makeString("Default")},
            {"chain", chain},
            {"automation", core::Json::makeArray()},
            {"outputGainDb", juce::var{document.output_gain.db}},
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

    juce::FileInputStream tone_document_file{common::core::juceFileFromPath(*tone_document_path)};
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

    const std::size_t plugin_count = static_cast<std::size_t>(chain_json.size());
    if (plugin_count > max_signal_chain_plugins)
    {
        return std::unexpected{LiveRigError{
            LiveRigErrorCode::PluginChainLimitExceeded,
            pluginChainLimitExceededMessage(plugin_count),
        }};
    }

    const std::filesystem::path expected_state_directory =
        toneDocumentStateDirectory(std::filesystem::path{tone_document_ref});
    ToneDocument document;
    document.chain.reserve(plugin_count);
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

        const auto id = core::Json::tryReadString(plugin_json, "id");
        const auto tracktion_state = core::Json::tryReadString(plugin_json, "tracktionState");
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
        // Block placement is editor-owned metadata; carry the raw value through opaquely and leave
        // interpretation (validity, gap rules, fallback) to the editor. Absent or negative values
        // default to zero and are resolved by the editor like any other invalid placement.
        const std::optional<std::int64_t> block_index_value =
            core::Json::tryReadInt64(plugin_json, "blockIndex");
        const std::size_t block_index = block_index_value.has_value() && *block_index_value >= 0
                                            ? static_cast<std::size_t>(*block_index_value)
                                            : 0;
        document.chain.push_back(
            PluginRecord{
                .id = *id,
                .identity =
                    identity_json.isObject() ? readPluginIdentity(identity_json) : PluginIdentity{},
                .tracktion_state_ref = *tracktion_state,
                .block_index = block_index,
                .display_type_override =
                    core::Json::readOptionalString(plugin_json, "displayTypeOverride"),
            });
    }

    // Gain fields are additive; older tones without them default to 0.0 dB. Legacy inputGainDb
    // is intentionally ignored because input calibration is app-local.
    document.output_gain = clampGain(
        Gain{core::Json::readOptionalDouble(default_slot_json, "outputGainDb", defaultGainDb())});

    return document;
}

// Writes the v1 tone document JSON file.
[[nodiscard]] std::expected<void, LiveRigError> writeToneDocument(
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
    const std::unique_ptr<juce::XmlElement> xml =
        juce::parseXML(common::core::juceFileFromPath(plugin_state_path));
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

// Converts text to the opaque byte shape carried by PluginInstanceState.
[[nodiscard]] std::vector<std::byte> bytesFromString(std::string_view text)
{
    std::vector<std::byte> bytes;
    bytes.reserve(text.size());
    for (const char character : text)
    {
        bytes.push_back(static_cast<std::byte>(static_cast<unsigned char>(character)));
    }

    return bytes;
}

// Converts an opaque memento payload back into text for Tracktion XML parsing.
[[nodiscard]] std::string stringFromBytes(const std::vector<std::byte>& bytes)
{
    std::string text;
    text.reserve(bytes.size());
    for (const std::byte byte : bytes)
    {
        text.push_back(static_cast<char>(std::to_integer<unsigned char>(byte)));
    }

    return text;
}

// Serializes a live plugin ValueTree into the in-memory memento form used by editor undo.
[[nodiscard]] std::expected<PluginInstanceState, PluginHostError> makePluginInstanceState(
    const juce::ValueTree& plugin_state)
{
    const std::unique_ptr<juce::XmlElement> xml = plugin_state.createXml();
    if (xml == nullptr)
    {
        return std::unexpected{PluginHostError{
            PluginHostErrorCode::PluginStateCaptureFailed,
            "Could not serialize plugin state",
        }};
    }

    return PluginInstanceState{.opaque_data = bytesFromString(xml->toString().toStdString())};
}

// Parses a memento payload into Tracktion's plugin-state tree and rejects non-external states.
[[nodiscard]] std::expected<juce::ValueTree, PluginHostError> pluginStateTreeFromMemento(
    const PluginInstanceState& state)
{
    if (state.opaque_data.empty())
    {
        return std::unexpected{PluginHostError{
            PluginHostErrorCode::PluginStateRestoreFailed,
            "Plugin state is empty",
        }};
    }

    if (state.opaque_data.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()))
    {
        return std::unexpected{PluginHostError{
            PluginHostErrorCode::PluginStateRestoreFailed,
            "Plugin state is too large to parse",
        }};
    }

    const std::string xml_text = stringFromBytes(state.opaque_data);
    const juce::String xml_string =
        juce::String::fromUTF8(xml_text.data(), static_cast<int>(xml_text.size()));
    const std::unique_ptr<juce::XmlElement> xml = juce::parseXML(xml_string);
    if (xml == nullptr)
    {
        return std::unexpected{PluginHostError{
            PluginHostErrorCode::PluginStateRestoreFailed,
            "Could not parse plugin state",
        }};
    }

    juce::ValueTree plugin_state = juce::ValueTree::fromXml(*xml);
    if (!plugin_state.isValid() || !plugin_state.hasType(tracktion::IDs::PLUGIN) ||
        plugin_state[tracktion::IDs::type].toString() != tracktion::ExternalPlugin::xmlTypeName)
    {
        return std::unexpected{PluginHostError{
            PluginHostErrorCode::PluginStateRestoreFailed,
            "Plugin state is not an external plugin ValueTree",
        }};
    }

    return plugin_state;
}

// Reads the runtime instance id encoded in a captured Tracktion plugin tree, when one exists.
[[nodiscard]] std::string pluginInstanceIdFromState(const juce::ValueTree& plugin_state)
{
    const tracktion::EditItemID item_id = tracktion::EditItemID::fromID(plugin_state);
    return item_id.isValid() ? item_id.toString().toStdString() : std::string{};
}

// Copies serialized plugin state to an existing live plugin without changing its runtime id.
void copyPluginStatePreservingInstanceId(
    tracktion::Plugin& target_plugin, const juce::ValueTree& source_state)
{
    juce::ValueTree target_state = target_plugin.state;
    for (int index = target_state.getNumProperties(); --index >= 0;)
    {
        const juce::Identifier property_name = target_state.getPropertyName(index);
        if (property_name != tracktion::IDs::id)
        {
            target_state.removeProperty(property_name, nullptr);
        }
    }

    for (int index = 0; index < source_state.getNumProperties(); ++index)
    {
        const juce::Identifier property_name = source_state.getPropertyName(index);
        if (property_name != tracktion::IDs::id)
        {
            target_state.setProperty(
                property_name, source_state.getProperty(property_name), nullptr);
        }
    }

    target_state.removeAllChildren(nullptr);
    for (int index = 0; index < source_state.getNumChildren(); ++index)
    {
        target_state.addChild(source_state.getChild(index).createCopy(), index, nullptr);
    }
    target_plugin.itemID.writeID(target_state, nullptr);
}

// Observes one external plugin's Tracktion gestures and emits parameter value edits.
class PluginParameterValueTracker final : private tracktion::AutomatableParameter::Listener,
                                          private juce::Timer
{
public:
    using EmitEdit = std::function<void(PluginParameterEdit)>;
    using PendingChanged = std::function<void()>;
    using ShouldSuppress = std::function<bool()>;
    using NonGestureChanged = std::function<void()>;
    using CancelStateChanged = std::function<void()>;

    PluginParameterValueTracker(
        tracktion::ExternalPlugin& plugin, EmitEdit emit_edit, PendingChanged pending_changed,
        ShouldSuppress should_suppress, NonGestureChanged non_gesture_changed,
        CancelStateChanged cancel_state_changed)
        : m_plugin(plugin)
        , m_instance_id(plugin.itemID.toString().toStdString())
        , m_emit_edit(std::move(emit_edit))
        , m_pending_changed(std::move(pending_changed))
        , m_should_suppress(std::move(should_suppress))
        , m_non_gesture_changed(std::move(non_gesture_changed))
        , m_cancel_state_changed(std::move(cancel_state_changed))
    {
        const int parameter_count = m_plugin.getNumAutomatableParameters();
        m_parameters.reserve(static_cast<std::size_t>(std::max(parameter_count, 0)));
        for (int index = 0; index < parameter_count; ++index)
        {
            tracktion::AutomatableParameter::Ptr parameter =
                m_plugin.getAutomatableParameter(index);
            if (parameter == nullptr)
            {
                continue;
            }

            parameter->addListener(this);
            m_parameters.push_back(std::move(parameter));
        }
    }

    ~PluginParameterValueTracker() override
    {
        stopTimer();
        for (const tracktion::AutomatableParameter::Ptr& parameter : m_parameters)
        {
            if (parameter != nullptr)
            {
                parameter->removeListener(this);
            }
        }
    }

    [[nodiscard]] bool hasPendingEdit() const noexcept
    {
        return m_gesture_edit.has_value() || m_completed_gesture_edit.has_value();
    }

    void flushPendingEdit()
    {
        stopTimer();
        if (m_gesture_edit.has_value())
        {
            RH_LOG_INFO(
                "audio.engine",
                "Dropped in-flight plugin parameter gesture instance_id={}",
                m_instance_id);
            discardPendingEdit();
            return;
        }

        emitCompletedGestureEdit();
    }

    void discardPendingEdit()
    {
        stopTimer();
        m_gesture_edit.reset();
        m_completed_gesture_edit.reset();
        m_parameter_burst_promoted = false;
        m_open_gesture_count = 0;
        notifyPendingChanged();
    }

private:
    static constexpr auto g_gesture_coalesce = std::chrono::milliseconds{250};
    static constexpr auto g_promoted_burst_debounce = std::chrono::milliseconds{750};
    static constexpr auto g_post_gesture_tail_suppression = std::chrono::milliseconds{200};

    [[nodiscard]] static std::string labelHintFor(tracktion::AutomatableParameter& parameter)
    {
        return parameter.getParameterName().toStdString();
    }

    [[nodiscard]] static std::string parameterIdFor(tracktion::AutomatableParameter& parameter)
    {
        return parameter.paramID.toStdString();
    }

    [[nodiscard]] bool isSuppressed() const
    {
        return m_should_suppress && m_should_suppress();
    }

    [[nodiscard]] int parameterIndexFor(tracktion::AutomatableParameter& parameter) const
    {
        for (std::size_t index = 0; index < m_parameters.size(); ++index)
        {
            if (m_parameters[index].get() == &parameter)
            {
                return static_cast<int>(index);
            }
        }

        return -1;
    }

    [[nodiscard]] PluginParameterEdit makePendingEdit(tracktion::AutomatableParameter& parameter)
    {
        return PluginParameterEdit{
            .instance_id = m_instance_id,
            .parameter_id = parameterIdFor(parameter),
            .parameter_index = parameterIndexFor(parameter),
            .before_normalized = parameter.getCurrentNormalisedValue(),
            .after_normalized = parameter.getCurrentNormalisedValue(),
            .label_hint = labelHintFor(parameter),
        };
    }

    void notifyPendingChanged()
    {
        if (m_pending_changed)
        {
            m_pending_changed();
        }
    }

    void emitIfChanged(PluginParameterEdit edit)
    {
        if (edit.before_normalized == edit.after_normalized)
        {
            return;
        }

        if (m_emit_edit)
        {
            m_emit_edit(std::move(edit));
        }
    }

    void clearGestureState()
    {
        m_gesture_edit.reset();
        m_open_gesture_count = 0;
    }

    void noteStateBurstChanged()
    {
        if (m_non_gesture_changed)
        {
            m_non_gesture_changed();
        }
        startTimer(static_cast<int>(g_promoted_burst_debounce.count()));
    }

    void emitCompletedGestureEdit()
    {
        if (!m_completed_gesture_edit.has_value())
        {
            return;
        }

        PluginParameterEdit edit = std::move(*m_completed_gesture_edit);
        m_completed_gesture_edit.reset();
        if (m_cancel_state_changed)
        {
            m_cancel_state_changed();
        }
        RH_LOG_INFO(
            "audio.engine",
            "Plugin parameter edit completed instance_id={} label_hint={}",
            m_instance_id,
            edit.label_hint);
        emitIfChanged(std::move(edit));
        notifyPendingChanged();
    }

    void parameterChangeGestureBegin(tracktion::AutomatableParameter& parameter) override
    {
        if (isSuppressed())
        {
            return;
        }

        if (m_parameter_burst_promoted)
        {
            noteStateBurstChanged();
            return;
        }

        if (m_completed_gesture_edit.has_value())
        {
            if (m_completed_gesture_edit->parameter_id == parameterIdFor(parameter) &&
                m_completed_gesture_edit->parameter_index == parameterIndexFor(parameter))
            {
                m_gesture_edit = std::move(*m_completed_gesture_edit);
                m_completed_gesture_edit.reset();
                stopTimer();
            }
            else
            {
                RH_LOG_INFO(
                    "audio.engine",
                    "Promoted plugin parameter burst to state edit instance_id={}",
                    m_instance_id);
                m_completed_gesture_edit.reset();
                m_parameter_burst_promoted = true;
                noteStateBurstChanged();
                notifyPendingChanged();
                return;
            }
        }

        if (!m_gesture_edit.has_value())
        {
            m_gesture_edit = makePendingEdit(parameter);
            notifyPendingChanged();
            RH_LOG_INFO(
                "audio.engine",
                "Plugin parameter edit started instance_id={} label_hint={}",
                m_instance_id,
                m_gesture_edit->label_hint);
        }

        ++m_open_gesture_count;
    }

    void parameterChangeGestureEnd(tracktion::AutomatableParameter& parameter) override
    {
        if (isSuppressed())
        {
            discardPendingEdit();
            return;
        }

        if (m_parameter_burst_promoted)
        {
            noteStateBurstChanged();
            return;
        }

        if (m_open_gesture_count > 0)
        {
            --m_open_gesture_count;
        }
        m_ignore_non_gesture_until =
            std::chrono::steady_clock::now() + g_post_gesture_tail_suppression;
        if (m_open_gesture_count > 0)
        {
            return;
        }

        if (!m_gesture_edit.has_value())
        {
            notifyPendingChanged();
            return;
        }

        PluginParameterEdit edit = std::move(*m_gesture_edit);
        edit.after_normalized = parameter.getCurrentNormalisedValue();
        clearGestureState();

        if (edit.before_normalized != edit.after_normalized)
        {
            m_completed_gesture_edit = std::move(edit);
            RH_LOG_INFO(
                "audio.engine",
                "Held plugin parameter edit for coalescing instance_id={} label_hint={}",
                m_instance_id,
                m_completed_gesture_edit->label_hint);
            startTimer(static_cast<int>(g_gesture_coalesce.count()));
        }
        else
        {
            RH_LOG_INFO(
                "audio.engine",
                "Plugin parameter gesture yielded no delta; tracking state instance_id={} "
                "label_hint={}",
                m_instance_id,
                edit.label_hint);
            noteStateBurstChanged();
        }
        notifyPendingChanged();
    }

    void curveHasChanged(tracktion::AutomatableParameter& /*parameter*/) override
    {}

    void currentValueChanged(tracktion::AutomatableParameter& /*parameter*/) override
    {}

    void parameterChanged(tracktion::AutomatableParameter& parameter, float /*new_value*/) override
    {
        handleNonGestureChange(parameter);
    }

    void handleNonGestureChange(tracktion::AutomatableParameter& /*parameter*/)
    {
        if (isSuppressed())
        {
            return;
        }

        const auto now = std::chrono::steady_clock::now();
        if (m_gesture_edit.has_value() || now < m_ignore_non_gesture_until)
        {
            return;
        }

        if (m_non_gesture_changed)
        {
            m_non_gesture_changed();
        }
    }

    void timerCallback() override
    {
        stopTimer();
        if (m_parameter_burst_promoted)
        {
            m_parameter_burst_promoted = false;
            notifyPendingChanged();
            return;
        }

        emitCompletedGestureEdit();
    }

    tracktion::ExternalPlugin& m_plugin;
    std::string m_instance_id;
    EmitEdit m_emit_edit;
    PendingChanged m_pending_changed;
    ShouldSuppress m_should_suppress;
    NonGestureChanged m_non_gesture_changed;
    CancelStateChanged m_cancel_state_changed;
    std::vector<tracktion::AutomatableParameter::Ptr> m_parameters;
    std::optional<PluginParameterEdit> m_gesture_edit;
    std::optional<PluginParameterEdit> m_completed_gesture_edit;
    std::chrono::steady_clock::time_point m_ignore_non_gesture_until{};
    int m_open_gesture_count{0};
    bool m_parameter_burst_promoted{false};
};

// Captures both opaque state and parameter values for one hosted plugin.
struct PluginStateSnapshot
{
    PluginInstanceState state;
    std::vector<PluginParameterSnapshot> parameters;
};

// Observes plugin-wide processor changes and emits full-state edits for presets and file loads.
class PluginStateChangeTracker final : private tracktion::SelectableListener, private juce::Timer
{
public:
    using CaptureState = std::function<std::expected<PluginStateSnapshot, PluginHostError>(
        tracktion::ExternalPlugin&)>;
    using EmitEdit = std::function<void(PluginStateEdit)>;
    using PendingChanged = std::function<void()>;
    using ShouldSuppress = std::function<bool()>;

    PluginStateChangeTracker(
        tracktion::ExternalPlugin& plugin, CaptureState capture_state, EmitEdit emit_edit,
        PendingChanged pending_changed, ShouldSuppress should_suppress)
        : m_plugin(plugin)
        , m_instance_id(plugin.itemID.toString().toStdString())
        , m_capture_state(std::move(capture_state))
        , m_emit_edit(std::move(emit_edit))
        , m_pending_changed(std::move(pending_changed))
        , m_should_suppress(std::move(should_suppress))
    {
        refreshBaseline();
        m_plugin.addSelectableListener(this);
    }

    ~PluginStateChangeTracker() override
    {
        stopTimer();
        if (tracktion::Selectable::isSelectableValid(&m_plugin))
        {
            m_plugin.removeSelectableListener(this);
        }
    }

    [[nodiscard]] bool hasPendingEdit() const noexcept
    {
        return m_before.has_value();
    }

    void flushPendingEdit()
    {
        stopTimer();
        settlePendingEdit();
    }

    void noteParameterStateChanged()
    {
        beginPendingEdit();
    }

    void cancelPendingParameterStateChange()
    {
        if (!m_before.has_value())
        {
            return;
        }

        stopTimer();
        m_before.reset();
        refreshBaseline();
        notifyPendingChanged();
    }

private:
    static constexpr auto g_state_change_debounce = std::chrono::milliseconds{750};

    [[nodiscard]] bool isSuppressed() const
    {
        return m_should_suppress && m_should_suppress();
    }

    [[nodiscard]] std::expected<PluginStateSnapshot, PluginHostError> captureState()
    {
        if (!m_capture_state)
        {
            return std::unexpected{PluginHostError{
                PluginHostErrorCode::PluginStateCaptureFailed,
                "Plugin state tracker has no capture callback",
            }};
        }

        return m_capture_state(m_plugin);
    }

    void notifyPendingChanged()
    {
        if (m_pending_changed)
        {
            m_pending_changed();
        }
    }

    void refreshBaseline()
    {
        auto captured = captureState();
        if (!captured.has_value())
        {
            RH_LOG_WARNING(
                "audio.engine",
                "Could not refresh plugin state baseline instance_id={} detail={}",
                m_instance_id,
                captured.error().message);
            m_baseline.reset();
            return;
        }

        m_baseline = std::move(*captured);
    }

    void selectableObjectChanged(tracktion::Selectable* selectable) override
    {
        if (selectable != &m_plugin || isSuppressed())
        {
            return;
        }

        beginPendingEdit();
    }

    void beginPendingEdit()
    {
        if (isSuppressed())
        {
            return;
        }

        if (!m_before.has_value())
        {
            if (!m_baseline.has_value())
            {
                refreshBaseline();
            }

            if (!m_baseline.has_value())
            {
                return;
            }

            m_before = *m_baseline;
            notifyPendingChanged();
            RH_LOG_INFO(
                "audio.engine",
                "Plugin state edit started instance_id={} label_hint={}",
                m_instance_id,
                m_plugin.getName().toStdString());
        }

        startTimer(static_cast<int>(g_state_change_debounce.count()));
    }

    void selectableObjectAboutToBeDeleted(tracktion::Selectable* selectable) override
    {
        if (selectable == &m_plugin)
        {
            stopTimer();
            m_before.reset();
            m_baseline.reset();
        }
    }

    void settlePendingEdit()
    {
        if (!m_before.has_value())
        {
            return;
        }

        auto after = captureState();
        PluginStateSnapshot before = std::move(*m_before);
        m_before.reset();
        if (!after.has_value())
        {
            RH_LOG_WARNING(
                "audio.engine",
                "Could not complete plugin state edit instance_id={} detail={}",
                m_instance_id,
                after.error().message);
            refreshBaseline();
            notifyPendingChanged();
            return;
        }

        m_baseline = *after;
        if ((before.state != after->state || before.parameters != after->parameters) && m_emit_edit)
        {
            RH_LOG_INFO(
                "audio.engine",
                "Plugin state edit completed instance_id={} label_hint={}",
                m_instance_id,
                m_plugin.getName().toStdString());
            m_emit_edit(
                PluginStateEdit{
                    .instance_id = m_instance_id,
                    .before = std::move(before.state),
                    .after = std::move(after->state),
                    .before_parameters = std::move(before.parameters),
                    .after_parameters = std::move(after->parameters),
                    .label_hint = m_plugin.getName().toStdString(),
                });
        }

        notifyPendingChanged();
    }

    void timerCallback() override
    {
        stopTimer();
        settlePendingEdit();
    }

    tracktion::ExternalPlugin& m_plugin;
    std::string m_instance_id;
    CaptureState m_capture_state;
    EmitEdit m_emit_edit;
    PendingChanged m_pending_changed;
    ShouldSuppress m_should_suppress;
    std::optional<PluginStateSnapshot> m_baseline;
    std::optional<PluginStateSnapshot> m_before;
};

// Builds the opaque project-owned candidate that UI and core callers can pass back to the host.
[[nodiscard]] PluginCandidate makePluginCandidate(
    const juce::PluginDescription& description, const std::filesystem::path& plugin_path)
{
    return PluginCandidate{
        .id = description.createIdentifierString().toStdString(),
        .name = description.name.toStdString(),
        .manufacturer = description.manufacturerName.toStdString(),
        .format_name = description.pluginFormatName.toStdString(),
        .category = description.category.toStdString(),
        .file_path = vst3DisplayPath(plugin_path),
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

    // Creates Rock Hero-owned structural plugins from Tracktion plugin state.
    tracktion::Plugin::Ptr createCustomPlugin(tracktion::PluginCreationInfo info) override
    {
        if (info.state[tracktion::IDs::type].toString() == LiveRigGainPlugin::xmlTypeName)
        {
            // Tracktion's custom-plugin factory adopts this into Plugin::Ptr.
            return tracktion::Plugin::Ptr{new LiveRigGainPlugin{info}};
        }

        return {};
    }
};

// Top-level JUCE window that Tracktion owns through PluginWindowState::pluginWindow.
class PluginWindow final : public juce::DocumentWindow
{
public:
    // Creates a window only when Tracktion can supply a concrete plugin editor component.
    [[nodiscard]] static std::unique_ptr<juce::Component> create(
        tracktion::Plugin& plugin, PluginWindowCommandDispatcher command_dispatcher)
    {
        std::unique_ptr<tracktion::Plugin::EditorComponent> editor = plugin.createEditor();
        if (editor == nullptr)
        {
            return {};
        }

        return std::make_unique<PluginWindow>(
            plugin, std::move(editor), std::move(command_dispatcher));
    }

    // Takes ownership of Tracktion's editor component and lets plugin size changes drive bounds.
    PluginWindow(
        tracktion::Plugin& plugin, std::unique_ptr<tracktion::Plugin::EditorComponent> editor,
        PluginWindowCommandDispatcher command_dispatcher)
        : juce::DocumentWindow(
              plugin.getName(), juce::Colours::darkgrey,
              juce::DocumentWindow::closeButton | juce::DocumentWindow::minimiseButton)
        , m_plugin(plugin)
        , m_window_state(*plugin.windowState)
        , m_command_dispatcher(std::move(command_dispatcher))
    {
        setUsingNativeTitleBar(true);
        setWantsKeyboardFocus(true);

        // Configure the default ResizableWindow constrainer as a backstop. setEditor() will
        // replace this with the plugin editor's own constrainer when one is supplied; these
        // values only take effect for editors that allow resizing but don't provide a
        // constrainer of their own. The onscreen amounts also guard against restored bounds
        // landing on a monitor that no longer exists â€” the title bar (top) must stay fully
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
#if JUCE_WINDOWS
        registerWindowsShortcutWindow();
#endif
    }

    PluginWindow(const PluginWindow&) = delete;
    PluginWindow& operator=(const PluginWindow&) = delete;
    PluginWindow(PluginWindow&&) = delete;
    PluginWindow& operator=(PluginWindow&&) = delete;

    // Flushes any plugin state touched by the editor before Tracktion releases the window.
    ~PluginWindow() override
    {
#if JUCE_WINDOWS
        unregisterWindowsShortcutWindow();
#endif
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
    // UIBehaviour::recreatePluginWindowContentAsync is contractual â€” Tracktion depends on this
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

    // Forwards shortcuts that JUCE receives before the plugin editor consumes them.
    bool keyPressed(const juce::KeyPress& key) override
    {
        if (handleCommandShortcut(key, "window"))
        {
            return true;
        }

        return juce::DocumentWindow::keyPressed(key);
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
    bool handleCommandShortcut(const juce::KeyPress& key, std::string_view source)
    {
        if (isUndoShortcut(key))
        {
            postCommandShortcut(PluginWindowCommand::Undo, source);
            return true;
        }

        if (isRedoShortcut(key))
        {
            postCommandShortcut(PluginWindowCommand::Redo, source);
            return true;
        }

        return false;
    }

    [[nodiscard]] static std::string_view commandName(PluginWindowCommand command) noexcept
    {
        switch (command)
        {
            case PluginWindowCommand::Undo:
            {
                return "Undo";
            }
            case PluginWindowCommand::Redo:
            {
                return "Redo";
            }
        }

        return "Unknown";
    }

    void dispatchCommandShortcut(PluginWindowCommand command, std::string_view source)
    {
        if (!m_command_dispatcher)
        {
            return;
        }

        RH_LOG_INFO(
            "audio.engine",
            "Plugin window shortcut forwarded source={} command={}",
            source,
            commandName(command));
        m_command_dispatcher(command);
    }

    void postCommandShortcut(PluginWindowCommand command, std::string_view source)
    {
        const juce::Component::SafePointer<PluginWindow> safe_this{this};
        const std::string source_text{source};
        juce::MessageManager::callAsync([safe_this, command, source_text] {
            if (auto* const window = safe_this.getComponent())
            {
                window->dispatchCommandShortcut(command, source_text);
            }
        });
    }

#if JUCE_WINDOWS
    [[nodiscard]] static bool isKeyDown(int virtual_key) noexcept
    {
        return (GetKeyState(virtual_key) & 0x8000) != 0;
    }

    [[nodiscard]] static std::optional<PluginWindowCommand> commandForWindowsKeyMessage(
        const MSG& message)
    {
        if (message.message != WM_KEYDOWN && message.message != WM_SYSKEYDOWN)
        {
            return std::nullopt;
        }

        constexpr LPARAM repeat_flag = 1LL << 30;
        if ((message.lParam & repeat_flag) != 0)
        {
            return std::nullopt;
        }

        if (!isKeyDown(VK_CONTROL) || isKeyDown(VK_MENU) || isKeyDown(VK_SHIFT))
        {
            return std::nullopt;
        }

        if (message.wParam == 'Z')
        {
            return PluginWindowCommand::Undo;
        }

        if (message.wParam == 'Y')
        {
            return PluginWindowCommand::Redo;
        }

        return std::nullopt;
    }

    [[nodiscard]] bool ownsNativeWindow(HWND window) const
    {
        const juce::ComponentPeer* const peer = getPeer();
        if (peer == nullptr)
        {
            return false;
        }

        auto* const native_handle = static_cast<HWND>(peer->getNativeHandle());
        return native_handle != nullptr &&
               (window == native_handle || IsChild(native_handle, window) != 0);
    }

    [[nodiscard]] static PluginWindow* windowForNativeMessage(HWND window)
    {
        for (PluginWindow* const plugin_window : s_windows_hook_state.windows)
        {
            if (plugin_window != nullptr && plugin_window->ownsNativeWindow(window))
            {
                return plugin_window;
            }
        }

        return nullptr;
    }

    static LRESULT CALLBACK windowsShortcutHook(int code, WPARAM w_param, LPARAM l_param)
    {
        if (code >= 0 && w_param == PM_REMOVE)
        {
            auto* const message = reinterpret_cast<MSG*>(l_param);
            if (message != nullptr)
            {
                const std::optional<PluginWindowCommand> command =
                    commandForWindowsKeyMessage(*message);
                if (command.has_value())
                {
                    if (PluginWindow* const window = windowForNativeMessage(message->hwnd);
                        window != nullptr)
                    {
                        window->postCommandShortcut(*command, "native_hook");
                        message->message = WM_NULL;
                        message->wParam = 0;
                        message->lParam = 0;
                    }
                }
            }
        }

        return CallNextHookEx(s_windows_hook_state.hook, code, w_param, l_param);
    }

    void registerWindowsShortcutWindow()
    {
        s_windows_hook_state.windows.push_back(this);
        if (s_windows_hook_state.hook == nullptr)
        {
            s_windows_hook_state.hook = SetWindowsHookExW(
                WH_GETMESSAGE, windowsShortcutHook, nullptr, GetCurrentThreadId());
        }
    }

    void unregisterWindowsShortcutWindow()
    {
        std::erase(s_windows_hook_state.windows, this);
        if (s_windows_hook_state.windows.empty() && s_windows_hook_state.hook != nullptr)
        {
            UnhookWindowsHookEx(s_windows_hook_state.hook);
            s_windows_hook_state.hook = nullptr;
        }
    }

    struct WindowsHookState
    {
        HHOOK hook{};
        std::vector<PluginWindow*> windows;
    };

    inline static WindowsHookState s_windows_hook_state{};
#endif

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

    // Routes host-window commands to the application without coupling this adapter to editor-core.
    PluginWindowCommandDispatcher m_command_dispatcher;

    // Prevents construction-time resized callbacks from overwriting Tracktion's default position.
    bool m_update_stored_bounds = false;
};

// Supplies Tracktion with Rock Hero's minimal plugin editor window implementation.
class RockHeroUIBehaviour final : public tracktion::UIBehaviour
{
public:
    explicit RockHeroUIBehaviour(PluginWindowCommandDispatcher command_dispatcher)
        : m_command_dispatcher(std::move(command_dispatcher))
    {}

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

        return PluginWindow::create(plugin_window_state->plugin, m_command_dispatcher);
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

private:
    PluginWindowCommandDispatcher m_command_dispatcher;
};

// Opens an asset through Tracktion only long enough to validate it and read its duration.
[[nodiscard]] std::expected<common::core::TimeDuration, SongAudioError> readAudioDuration(
    tracktion::Engine& engine, const common::core::AudioAsset& audio_asset)
{
    const juce::File file = common::core::juceFileFromPath(audio_asset.path);
    if (!file.existsAsFile())
    {
        return std::unexpected{SongAudioError{
            SongAudioErrorCode::UnreadableAudioFile,
            "Backing audio file does not exist: " + pathToUtf8String(audio_asset.path)
        }};
    }

    const tracktion::AudioFile audio_file(engine, file);
    if (!audio_file.isValid())
    {
        return std::unexpected{SongAudioError{
            SongAudioErrorCode::UnreadableAudioFile,
            "Backing audio file could not be decoded: " + pathToUtf8String(audio_asset.path)
        }};
    }

    const common::core::TimeDuration asset_duration{audio_file.getLength()};
    if (asset_duration.seconds <= 0.0)
    {
        return std::unexpected{SongAudioError{
            SongAudioErrorCode::InvalidAudioDuration,
            "Backing audio file has no positive duration: " + pathToUtf8String(audio_asset.path)
        }};
    }

    return asset_duration;
}

// Owns one Tracktion meter client and converts its most recent peak window into a project value.
class MeterReader
{
public:
    MeterReader() = default;
    MeterReader(const MeterReader&) = delete;
    MeterReader& operator=(const MeterReader&) = delete;
    MeterReader(MeterReader&&) = delete;
    MeterReader& operator=(MeterReader&&) = delete;

    // Detaches from Tracktion before the reader's client storage is destroyed.
    ~MeterReader()
    {
        detach();
    }

    // Moves this reader to a new Tracktion measurer when playback graphs or plugins are rebuilt.
    void attach(tracktion::LevelMeasurer* measurer)
    {
        if (m_measurer == measurer)
        {
            return;
        }

        detach();
        if (measurer == nullptr)
        {
            return;
        }

        measurer->addClient(m_client);
        m_measurer = measurer;
    }

    // Removes the client from the current Tracktion measurer, if one is still attached.
    void detach()
    {
        if (m_measurer != nullptr)
        {
            m_measurer->removeClient(m_client);
            m_measurer = nullptr;
        }
        m_client.reset();
    }

    // Reads and clears the peak window accumulated since the last snapshot.
    [[nodiscard]] AudioMeterLevel read()
    {
        if (m_measurer == nullptr)
        {
            return {};
        }

        constexpr int max_channels = tracktion::LevelMeasurer::Client::maxNumChannels;
        const int channel_count = std::clamp(m_client.getNumChannelsUsed(), 0, max_channels);
        double peak_db = minimumAudioMeterDb();
        for (int channel = 0; channel < channel_count; ++channel)
        {
            const tracktion::DbTimePair level = m_client.getAndClearAudioLevel(channel);
            if (std::isfinite(level.dB))
            {
                peak_db = std::max(peak_db, static_cast<double>(level.dB));
            }
        }

        return AudioMeterLevel{
            .peak_db = std::clamp(peak_db, minimumAudioMeterDb(), 12.0),
            .clipping = peak_db >= clippingAudioMeterDb(),
        };
    }

private:
    // Tracktion-owned measurer currently feeding this reader.
    tracktion::LevelMeasurer* m_measurer{};

    // Client object registered with the Tracktion measurer while attached.
    tracktion::LevelMeasurer::Client m_client;
};

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

    // Stable IDs for structural live-rig plugins around the external plugin chain. These are
    // hidden from PluginChainEntry snapshots and from the removable plugin rows in the editor.
    tracktion::EditItemID m_input_gain_plugin_id;
    tracktion::EditItemID m_input_meter_plugin_id;
    tracktion::EditItemID m_output_gain_plugin_id;
    tracktion::EditItemID m_output_meter_plugin_id;

    // Structural master-output meter, living on the edit master plugin list rather than the
    // instrument track. It rides a stable measurer (unlike the churning EditPlaybackContext), so the
    // UI meter read never re-registers a client onto a measurer that is mid-rebuild.
    tracktion::EditItemID m_master_meter_plugin_id;

    // Meter readers registered with Tracktion measurers on demand by audioMeterSnapshot().
    mutable MeterReader m_input_meter_reader;
    mutable MeterReader m_output_meter_reader;
    mutable MeterReader m_master_meter_reader;
    mutable MeterReader m_raw_input_meter_reader;

    // Duration of the loaded audio, used to clamp seeks and detect end-of-file.
    double m_loaded_length_seconds{0.0};

    // Last coarse state used to suppress duplicate listener notifications.
    TransportState m_last_notified_transport_state{};

    // Explicit gate for processed live guitar monitoring. Calibration owns this gate.
    bool m_live_input_monitoring_enabled{false};

    // Explicit gate for unprocessed calibration monitoring through the backing track.
    bool m_calibration_input_monitoring_enabled{false};

    // Message-thread listener list for the project-owned ITransport listener surface.
    juce::ListenerList<ITransport::Listener> m_transport_listeners;

    // Message-thread listener list for audio-device configuration changes.
    juce::ListenerList<IAudioDeviceConfiguration::Listener> m_audio_device_listeners;

    // Observer installed by editor-core for completed plugin-parameter value edits.
    PluginParameterEditObserver m_plugin_parameter_edit_observer;

    // Observer installed by editor-core for completed plugin-wide state edits.
    PluginStateEditObserver m_plugin_state_edit_observer;

    // Observer installed by editor-core for Undo/Redo shortcuts from plugin editor windows.
    PluginWindowCommandObserver m_plugin_window_command_observer;

    // Per-external-plugin parameter observers for the user-visible live-rig chain.
    std::vector<std::unique_ptr<PluginParameterValueTracker>> m_plugin_parameter_trackers;

    // Per-external-plugin state observers for processor-wide preset/file changes.
    std::vector<std::unique_ptr<PluginStateChangeTracker>> m_plugin_state_trackers;

    // Guards programmatic restore/load paths from recording plugin undo entries.
    bool m_plugin_parameter_observation_suppressed{false};

    // Tracktion can deliver parameter notifications asynchronously after a host-driven apply.
    std::chrono::steady_clock::time_point m_plugin_parameter_observation_suppressed_until{};

    // Last aggregate pending state reported to the editor observer.
    bool m_plugin_parameter_pending_notified{false};

    // Coalesces JUCE audio-device callbacks so Tracktion route repair runs after callback unwinds.
    bool m_audio_device_configuration_refresh_pending{false};

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

        // Structural live-rig plugins are managed explicitly rather than relying on Tracktion's
        // default plugin insertion.
        constexpr bool add_default_plugins = false;
        const tracktion::AudioTrack::Ptr instrument_track = m_edit->insertNewAudioTrack(
            tracktion::TrackInsertPoint::getEndOfTracks(*m_edit), nullptr, add_default_plugins);

        if (instrument_track != nullptr)
        {
            instrument_track->setName("Instrument");
            m_instrument_track_id = instrument_track->itemID;
            if (auto structural_created = createStructuralLiveRigPlugins();
                !structural_created.has_value())
            {
                logInstrumentMonitoringFailure(toJuceString(structural_created.error().message));
            }
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
            scheduleAudioDeviceConfigurationRefresh();
            return;
        }
        updateTransportState();
    }

    // Schedules device-change repair outside JUCE's AudioDeviceManager callback stack.
    void scheduleAudioDeviceConfigurationRefresh()
    {
        if (m_audio_device_configuration_refresh_pending)
        {
            return;
        }

        m_audio_device_configuration_refresh_pending = true;
        const std::weak_ptr<bool> alive_source{m_alive};
        const bool refresh_posted = juce::MessageManager::callAsync([this, alive = alive_source] {
            if (alive.expired())
            {
                return;
            }

            m_audio_device_configuration_refresh_pending = false;
            handleAudioDeviceConfigurationRefresh();
        });
        if (refresh_posted)
        {
            return;
        }

        m_audio_device_configuration_refresh_pending = false;
        logInstrumentMonitoringFailure("audio device refresh could not be posted");
        if (juce::MessageManager::existsAndIsCurrentThread())
        {
            handleAudioDeviceConfigurationRefresh();
        }
    }

    // Repairs Tracktion's device cache and notifies editor listeners after JUCE has changed routes.
    void handleAudioDeviceConfigurationRefresh()
    {
        m_live_input_monitoring_enabled = false;
        m_calibration_input_monitoring_enabled = false;
        detachInstrumentMonitoringRoute();
        m_engine->getDeviceManager().dispatchPendingUpdates();
        m_audio_device_listeners.call(
            &IAudioDeviceConfiguration::Listener::onAudioDeviceConfigurationChanged);
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

    // Scans one selected plugin file through Tracktion's JUCE-backed known-plugin list. This is
    // used by lazy browser adds and message-thread live-rig restore; callers must keep it off the
    // realtime audio thread and avoid concurrent access to Tracktion's known-plugin list.
    [[nodiscard]] std::expected<std::vector<PluginCandidate>, PluginHostError>
    scanPluginFileForCandidates(const std::filesystem::path& plugin_path)
    {
        const std::filesystem::path scan_path = vst3DisplayPath(plugin_path).lexically_normal();
        const juce::File plugin_file = common::core::juceFileFromPath(scan_path);
        if (scan_path.empty() || !plugin_file.exists())
        {
            return std::unexpected{PluginHostError{
                PluginHostErrorCode::MissingPluginFile,
                "Plugin file does not exist: " + scan_path.string()
            }};
        }

        try
        {
            constexpr auto* vst3_format_name = "VST3";
            auto& plugin_manager = m_engine->getPluginManager();
            bool scan_session_finished = false;
            const auto finish_scan_session = [&plugin_manager, &scan_session_finished] {
                if (!scan_session_finished)
                {
                    plugin_manager.knownPluginList.scanFinished();
                    scan_session_finished = true;
                }
            };
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
                    finish_scan_session();
                    return std::unexpected{PluginHostError{
                        PluginHostErrorCode::PluginScanFailed,
                        "Plugin scan timed out after " +
                            std::to_string(g_plugin_scan_timeout.count()) +
                            " seconds: " + scan_path.string()
                    }};
                }
            }

            if (!has_vst3_format)
            {
                finish_scan_session();
                return std::unexpected{PluginHostError{
                    PluginHostErrorCode::PluginScanFailed,
                    "VST3 plugin hosting is not enabled in this build"
                }};
            }

            finish_scan_session();

            std::vector<PluginCandidate> plugin_candidates;
            plugin_candidates.reserve(static_cast<std::size_t>(found_descriptions.size()));

            for (const juce::PluginDescription* description : found_descriptions)
            {
                if (description != nullptr && description->pluginFormatName == vst3_format_name)
                {
                    plugin_candidates.push_back(makePluginCandidate(
                        *description, pluginPathFromIdentifier(description->fileOrIdentifier)));
                }
            }

            if (plugin_candidates.empty())
            {
                return std::unexpected{PluginHostError{
                    PluginHostErrorCode::NoCompatiblePlugin,
                    "No VST3 plugin was found in: " + scan_path.string()
                }};
            }

            return plugin_candidates;
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

    [[nodiscard]] juce::AudioPluginFormat* vst3PluginFormat() const
    {
        constexpr auto* vst3_format_name = "VST3";
        for (juce::AudioPluginFormat* const format :
             m_engine->getPluginManager().pluginFormatManager.getFormats())
        {
            if (format != nullptr && format->getName() == vst3_format_name)
            {
                return format;
            }
        }

        return nullptr;
    }

    [[nodiscard]] juce::File pluginScanDeadMansPedalFile() const
    {
        return m_engine->getPropertyStorage().getAppCacheFolder().getChildFile(
            "PluginScanDeadMansPedal.txt");
    }

    [[nodiscard]] static juce::FileSearchPath pluginSearchPathFromRoots(
        const std::vector<std::filesystem::path>& roots)
    {
        juce::FileSearchPath search_path;
        for (const std::filesystem::path& root : roots)
        {
            if (!root.empty())
            {
                search_path.addIfNotAlreadyThere(common::core::juceFileFromPath(root));
            }
        }

        search_path.removeRedundantPaths();
        return search_path;
    }

    [[nodiscard]] static std::filesystem::path pluginPathFromIdentifier(
        const juce::String& file_or_identifier)
    {
        return common::core::pathFromJuceString(file_or_identifier);
    }

    [[nodiscard]] std::expected<juce::StringArray, PluginHostError> scanVst3SearchPath(
        juce::FileSearchPath search_path,
        const PluginCatalogScanProgressCallback& progress_callback,
        const common::core::CancellationToken& cancel = {})
    {
        juce::AudioPluginFormat* const format = vst3PluginFormat();
        if (format == nullptr)
        {
            return std::unexpected{PluginHostError{
                PluginHostErrorCode::PluginScanFailed,
                "VST3 plugin hosting is not enabled in this build"
            }};
        }

        const auto scan_started_at = std::chrono::steady_clock::now();
        search_path.removeRedundantPaths();
        juce::StringArray files = format->searchPathsForPlugins(search_path, true, true);
        files.removeEmptyStrings();
        files.removeDuplicates(true);
        const std::size_t total_plugins = static_cast<std::size_t>(files.size());

        try
        {
            auto& plugin_manager = m_engine->getPluginManager();
            juce::PluginDirectoryScanner scanner{
                plugin_manager.knownPluginList,
                *format,
                juce::FileSearchPath{},
                true,
                pluginScanDeadMansPedalFile(),
                true
            };
            scanner.setFilesOrIdentifiersToScan(files);

            // Progress is reported before scanning so the active path names the file about to be
            // validated. Asking the scanner for the next file keeps the path and any timeout
            // message aligned with its own dead-man-pedal reordering. For VST3 the returned
            // identifier is the file path.
            for (std::size_t completed_plugins = 0; completed_plugins < total_plugins;
                 ++completed_plugins)
            {
                // Stop at the next candidate boundary on cancellation. Candidates already validated
                // stay in the known-plugin list, so a cancelled scan still keeps partial progress.
                if (cancel.isCancelled())
                {
                    break;
                }

                const juce::String file_or_identifier =
                    scanner.getNextPluginFileThatWillBeScanned();
                reportPluginCatalogScanProgress(
                    progress_callback,
                    completed_plugins,
                    total_plugins,
                    pluginPathFromIdentifier(file_or_identifier));

                juce::String name_of_plugin_being_scanned;
                PluginScanTimeout scan_timeout{
                    [&plugin_manager] { plugin_manager.abortCurrentPluginScan(); },
                    g_plugin_scan_timeout
                };
                scanner.scanNextFile(true, name_of_plugin_being_scanned);
                scan_timeout.finish();

                if (scan_timeout.timedOut())
                {
                    plugin_manager.knownPluginList.removeFromBlacklist(file_or_identifier);
                    return std::unexpected{PluginHostError{
                        PluginHostErrorCode::PluginScanFailed,
                        "Plugin scan timed out after " +
                            std::to_string(g_plugin_scan_timeout.count()) +
                            " seconds: " + file_or_identifier.toStdString()
                    }};
                }
            }

            reportPluginCatalogScanProgress(progress_callback, total_plugins, total_plugins, {});
        }
        catch (const std::exception& error)
        {
            return std::unexpected{PluginHostError{
                PluginHostErrorCode::PluginScanFailed,
                std::string{"Plugin catalog scan failed: "} + error.what()
            }};
        }

        logPluginCatalogScanSummary(total_plugins, elapsedMilliseconds(scan_started_at));
        return files;
    }

    // Exposes Tracktion's in-memory known-plugin list without triggering filesystem scans.
    [[nodiscard]] std::vector<PluginCandidate> knownPluginCatalog() const
    {
        constexpr auto* vst3_format_name = "VST3";
        std::vector<PluginCandidate> plugin_candidates;
        std::unordered_set<std::string> seen_plugin_ids;
        std::unordered_set<std::string> seen_plugin_paths;
        const auto& known_types = m_engine->getPluginManager().knownPluginList.getTypes();
        plugin_candidates.reserve(static_cast<std::size_t>(known_types.size()));
        seen_plugin_ids.reserve(plugin_candidates.capacity());
        seen_plugin_paths.reserve(plugin_candidates.capacity());

        const auto append_candidate = [&plugin_candidates, &seen_plugin_ids, &seen_plugin_paths](
                                          PluginCandidate plugin_candidate) {
            const std::string path_key = normalizedPluginPathKey(plugin_candidate.file_path);
            if (seen_plugin_ids.contains(plugin_candidate.id) ||
                seen_plugin_paths.contains(path_key))
            {
                return;
            }

            seen_plugin_ids.insert(plugin_candidate.id);
            seen_plugin_paths.insert(path_key);
            plugin_candidates.push_back(std::move(plugin_candidate));
        };

        for (const juce::PluginDescription& description : known_types)
        {
            if (description.pluginFormatName != vst3_format_name)
            {
                continue;
            }

            append_candidate(makePluginCandidate(
                description, pluginPathFromIdentifier(description.fileOrIdentifier)));
        }

        return plugin_candidates;
    }

    [[nodiscard]] std::vector<PluginCandidate> knownPluginCatalogForScannedFiles(
        const juce::StringArray& scanned_files) const
    {
        std::unordered_set<std::string> scanned_paths;
        scanned_paths.reserve(static_cast<std::size_t>(scanned_files.size()));
        for (const juce::String& file_or_identifier : scanned_files)
        {
            scanned_paths.insert(
                normalizedPluginPathKey(pluginPathFromIdentifier(file_or_identifier)));
        }

        std::vector<PluginCandidate> plugin_candidates;
        for (PluginCandidate plugin_candidate : knownPluginCatalog())
        {
            if (scanned_paths.contains(normalizedPluginPathKey(plugin_candidate.file_path)))
            {
                plugin_candidates.push_back(std::move(plugin_candidate));
            }
        }

        return plugin_candidates;
    }

    // Inserts a selected plugin candidate into the instrument track's user-visible chain.
    [[nodiscard]] std::expected<PluginInsertResult, PluginHostError> insertPluginCandidateToTrack(
        const PluginCandidate& plugin_candidate, std::size_t chain_index);

    // Builds the authoritative user-visible plugin chain snapshot from Tracktion state.
    [[nodiscard]] PluginChainSnapshot pluginChainSnapshot() const
    {
        PluginChainSnapshot snapshot;
        const tracktion::AudioTrack* const instrument_track = instrumentTrack();
        if (instrument_track == nullptr)
        {
            return snapshot;
        }

        const tracktion::Plugin::Array plugins = instrument_track->pluginList.getPlugins();
        snapshot.plugins.reserve(static_cast<std::size_t>(plugins.size()));
        for (tracktion::Plugin* const plugin : plugins)
        {
            if (plugin == nullptr || isStructuralLiveRigPlugin(plugin))
            {
                continue;
            }

            snapshot.plugins.push_back(makePluginChainEntry(*plugin, snapshot.plugins.size()));
        }

        return snapshot;
    }

    // Counts user-visible plugins while optionally ignoring a plugin being moved.
    [[nodiscard]] std::size_t userVisiblePluginCount(
        const tracktion::Plugin* ignored_plugin = nullptr) const
    {
        const tracktion::AudioTrack* const instrument_track = instrumentTrack();
        if (instrument_track == nullptr)
        {
            return 0;
        }

        std::size_t count = 0;
        for (const tracktion::Plugin* const plugin : instrument_track->pluginList)
        {
            if (plugin != nullptr && plugin != ignored_plugin && !isStructuralLiveRigPlugin(plugin))
            {
                ++count;
            }
        }
        return count;
    }

    // Maps a user-visible insertion slot to the raw Tracktion plugin-list index.
    [[nodiscard]] std::expected<int, PluginHostError> tracktionIndexForUserPluginSlot(
        std::size_t chain_index, const tracktion::Plugin* ignored_plugin = nullptr) const
    {
        const tracktion::AudioTrack* const instrument_track = instrumentTrack();
        if (instrument_track == nullptr)
        {
            return std::unexpected{PluginHostError{PluginHostErrorCode::TrackMissing}};
        }

        std::size_t user_index = 0;
        for (int raw_index = 0; raw_index < instrument_track->pluginList.size(); ++raw_index)
        {
            const tracktion::Plugin* const plugin = instrument_track->pluginList[raw_index];
            if (plugin == nullptr || plugin == ignored_plugin || isStructuralLiveRigPlugin(plugin))
            {
                continue;
            }

            if (user_index == chain_index)
            {
                return raw_index;
            }
            ++user_index;
        }

        if (user_index != chain_index)
        {
            return std::unexpected{PluginHostError{PluginHostErrorCode::InvalidChainIndex}};
        }

        const tracktion::Plugin* const output_gain =
            findStructuralGainPlugin(m_output_gain_plugin_id);
        const int output_gain_index =
            output_gain != nullptr ? instrument_track->pluginList.indexOf(output_gain) : -1;
        if (output_gain_index < 0)
        {
            return std::unexpected{PluginHostError{
                PluginHostErrorCode::PluginInsertionFailed,
                "Structural live rig output gain plugin is missing",
            }};
        }
        return output_gain_index;
    }

    // Finds one plugin's current user-visible index, excluding structural live-rig plugins.
    [[nodiscard]] std::optional<std::size_t> userVisiblePluginIndexOf(
        const tracktion::Plugin* target_plugin) const
    {
        if (target_plugin == nullptr || isStructuralLiveRigPlugin(target_plugin))
        {
            return std::nullopt;
        }

        const tracktion::AudioTrack* const instrument_track = instrumentTrack();
        if (instrument_track == nullptr)
        {
            return std::nullopt;
        }

        std::size_t user_index = 0;
        for (const tracktion::Plugin* const plugin : instrument_track->pluginList)
        {
            if (plugin == nullptr || isStructuralLiveRigPlugin(plugin))
            {
                continue;
            }

            if (plugin == target_plugin)
            {
                return user_index;
            }
            ++user_index;
        }

        return std::nullopt;
    }

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

    // Reports whether the given plugin is one of the structural live-rig plugins managed here.
    [[nodiscard]] bool isStructuralLiveRigPlugin(const tracktion::Plugin* plugin) const
    {
        if (plugin == nullptr)
        {
            return false;
        }
        return plugin->itemID == m_input_gain_plugin_id ||
               plugin->itemID == m_input_meter_plugin_id ||
               plugin->itemID == m_output_gain_plugin_id ||
               plugin->itemID == m_output_meter_plugin_id;
    }

    // Finalizes a committed plugin removal after rollback is no longer possible.
    void commitPluginRemoval(tracktion::Plugin& plugin) const
    {
        if (auto* const macro_parameters = plugin.getMacroParameterList();
            macro_parameters != nullptr)
        {
            macro_parameters->hideMacroParametersFromTracks();
        }

        for (tracktion::Track* const track : tracktion::getAllTracks(*m_edit))
        {
            if (track != nullptr)
            {
                track->hideAutomatableParametersForSource(plugin.itemID);
            }
        }

        plugin.hideWindowForShutdown();
        plugin.deselect();
    }

    // Reports whether any tracked plugin currently holds an unsettled user edit.
    [[nodiscard]] bool hasPendingPluginParameterEdits() const
    {
        const bool has_parameter_edit = std::ranges::any_of(
            m_plugin_parameter_trackers,
            [](const std::unique_ptr<PluginParameterValueTracker>& tracker) {
                return tracker != nullptr && tracker->hasPendingEdit();
            });
        const bool has_state_edit = std::ranges::any_of(
            m_plugin_state_trackers, [](const std::unique_ptr<PluginStateChangeTracker>& tracker) {
                return tracker != nullptr && tracker->hasPendingEdit();
            });
        return has_parameter_edit || has_state_edit;
    }

    // Sends aggregate pending-state changes to editor-core.
    void notifyPluginParameterPendingStateChanged()
    {
        const bool pending = hasPendingPluginParameterEdits();
        if (pending == m_plugin_parameter_pending_notified)
        {
            return;
        }

        m_plugin_parameter_pending_notified = pending;
        if (m_plugin_parameter_edit_observer.pending_changed)
        {
            m_plugin_parameter_edit_observer.pending_changed(pending);
        }
    }

    [[nodiscard]] static std::vector<PluginParameterSnapshot> capturePluginParameters(
        tracktion::ExternalPlugin& plugin)
    {
        std::vector<PluginParameterSnapshot> parameters;
        const int parameter_count = plugin.getNumAutomatableParameters();
        parameters.reserve(static_cast<std::size_t>(std::max(parameter_count, 0)));
        for (int index = 0; index < parameter_count; ++index)
        {
            tracktion::AutomatableParameter::Ptr parameter = plugin.getAutomatableParameter(index);
            if (parameter == nullptr)
            {
                continue;
            }

            parameters.push_back(
                PluginParameterSnapshot{
                    .parameter_id = parameter->paramID.toStdString(),
                    .parameter_index = index,
                    .normalized_value = parameter->getCurrentNormalisedValue(),
                    .label_hint = parameter->getParameterName().toStdString(),
                });
        }
        return parameters;
    }

    [[nodiscard]] bool shouldSuppressPluginParameterObservation() const
    {
        return m_plugin_parameter_observation_suppressed ||
               (m_edit != nullptr && m_edit->getTransport().isPlaying()) ||
               std::chrono::steady_clock::now() < m_plugin_parameter_observation_suppressed_until;
    }

    // Emits a completed parameter value edit to editor-core unless the engine is restoring state.
    void emitPluginParameterEdit(PluginParameterEdit edit)
    {
        if (shouldSuppressPluginParameterObservation())
        {
            return;
        }

        if (m_plugin_parameter_edit_observer.edit_completed)
        {
            m_plugin_parameter_edit_observer.edit_completed(std::move(edit));
        }
    }

    // Emits a completed plugin-wide state edit to editor-core unless the engine is restoring state.
    void emitPluginStateEdit(PluginStateEdit edit)
    {
        if (shouldSuppressPluginParameterObservation())
        {
            return;
        }

        if (m_plugin_state_edit_observer.edit_completed)
        {
            m_plugin_state_edit_observer.edit_completed(std::move(edit));
        }
    }

    // Routes hosted plugin-window shortcuts to the current app-level command observer.
    void dispatchPluginWindowCommand(PluginWindowCommand command)
    {
        switch (command)
        {
            case PluginWindowCommand::Undo:
            {
                if (m_plugin_window_command_observer.undo_requested)
                {
                    m_plugin_window_command_observer.undo_requested();
                }
                break;
            }
            case PluginWindowCommand::Redo:
            {
                if (m_plugin_window_command_observer.redo_requested)
                {
                    m_plugin_window_command_observer.redo_requested();
                }
                break;
            }
        }
    }

    // Detaches all Tracktion plugin edit listeners and reports any aggregate pending transition.
    void clearPluginParameterObservers()
    {
        m_plugin_parameter_trackers.clear();
        m_plugin_state_trackers.clear();
        notifyPluginParameterPendingStateChanged();
    }

    // Rebuilds plugin edit listeners for user-visible external plugins only.
    void refreshPluginParameterObservers()
    {
        m_plugin_parameter_trackers.clear();
        m_plugin_state_trackers.clear();
        tracktion::AudioTrack* const instrument_track = instrumentTrack();
        if (instrument_track == nullptr)
        {
            notifyPluginParameterPendingStateChanged();
            return;
        }

        for (tracktion::Plugin* const plugin : instrument_track->pluginList)
        {
            if (plugin == nullptr || isStructuralLiveRigPlugin(plugin))
            {
                continue;
            }

            auto* const external_plugin = dynamic_cast<tracktion::ExternalPlugin*>(plugin);
            if (external_plugin == nullptr)
            {
                continue;
            }

            auto state_tracker = std::make_unique<PluginStateChangeTracker>(
                *external_plugin,
                [this](tracktion::ExternalPlugin& observed_plugin)
                    -> std::expected<PluginStateSnapshot, PluginHostError> {
                    observed_plugin.flushPluginStateToValueTree();
                    auto state = makePluginInstanceState(observed_plugin.state.createCopy());
                    if (!state.has_value())
                    {
                        return std::unexpected{std::move(state.error())};
                    }

                    return PluginStateSnapshot{
                        .state = std::move(*state),
                        .parameters = capturePluginParameters(observed_plugin),
                    };
                },
                [this](PluginStateEdit edit) { emitPluginStateEdit(std::move(edit)); },
                [this] { notifyPluginParameterPendingStateChanged(); },
                [this] { return shouldSuppressPluginParameterObservation(); });
            // The pointer targets the heap object owned by the unique_ptr, so vector growth does
            // not invalidate the callback target. Trackers are cleared in parameter-then-state
            // order before plugin-chain observer rebuilds.
            PluginStateChangeTracker* const state_tracker_ptr = state_tracker.get();
            m_plugin_state_trackers.push_back(std::move(state_tracker));
            m_plugin_parameter_trackers.push_back(
                std::make_unique<PluginParameterValueTracker>(
                    *external_plugin,
                    [this](PluginParameterEdit edit) { emitPluginParameterEdit(std::move(edit)); },
                    [this] { notifyPluginParameterPendingStateChanged(); },
                    [this] { return shouldSuppressPluginParameterObservation(); },
                    [state_tracker_ptr] { state_tracker_ptr->noteParameterStateChanged(); },
                    [state_tracker_ptr] {
                        state_tracker_ptr->cancelPendingParameterStateChange();
                    }));
        }

        notifyPluginParameterPendingStateChanged();
    }

    // Settles all pending plugin edits synchronously.
    //
    // Invariant: this iterates m_plugin_parameter_trackers while each flushPendingEdit() re-enters
    // editor-core synchronously (the completed-edit observer). That is safe only because render
    // (updateView) never synchronously mutates the plugin chain; a chain mutation here would call
    // clearPluginParameterObservers()/refreshPluginParameterObservers() and invalidate this loop's
    // iterator. Any render-triggered chain change must be enqueued for a later dispatch, not applied
    // under this loop. If that invariant is ever relaxed, snapshot the trackers before iterating.
    void flushPendingPluginParameterEdits()
    {
        for (const std::unique_ptr<PluginParameterValueTracker>& tracker :
             m_plugin_parameter_trackers)
        {
            if (tracker != nullptr)
            {
                tracker->flushPendingEdit();
            }
        }
        for (const std::unique_ptr<PluginStateChangeTracker>& tracker : m_plugin_state_trackers)
        {
            if (tracker != nullptr)
            {
                tracker->flushPendingEdit();
            }
        }
        notifyPluginParameterPendingStateChanged();
    }

    // Keeps plugin-list mutation, monitoring teardown, re-route, and failure routing in one path.
    template <typename Mutate, typename Rollback>
    [[nodiscard]] std::expected<void, PluginHostError> mutateAndReroutePluginChain(
        Mutate mutate, Rollback rollback, std::string_view route_rollback_context)
    {
        stopTransportAndReleaseContext();
        clearPluginParameterObservers();

        auto mutation_result = mutate();
        if (!mutation_result.has_value())
        {
            PluginChainMutationFailure failure = std::move(mutation_result.error());
            rebuildInstrumentMonitoringGraphBestEffort(failure.reroute_context);
            refreshPluginParameterObservers();
            return std::unexpected{std::move(failure.error)};
        }

        auto route_result = rebuildInstrumentMonitoringGraph();
        if (route_result.has_value())
        {
            refreshPluginParameterObservers();
            return {};
        }

        rollback();
        rebuildInstrumentMonitoringGraphBestEffort(route_rollback_context);
        refreshPluginParameterObservers();
        return std::unexpected{pluginHostErrorFromLiveInputError(route_result.error())};
    }

    // Finds a structural live-rig gain plugin by its stored EditItemID, or null if absent.
    [[nodiscard]] LiveRigGainPlugin* findStructuralGainPlugin(tracktion::EditItemID plugin_id) const
    {
        if (!plugin_id.isValid())
        {
            return nullptr;
        }
        const tracktion::AudioTrack* const instrument_track = instrumentTrack();
        if (instrument_track == nullptr)
        {
            return nullptr;
        }
        for (tracktion::Plugin* const plugin : instrument_track->pluginList)
        {
            if (plugin != nullptr && plugin->itemID == plugin_id)
            {
                return dynamic_cast<LiveRigGainPlugin*>(plugin);
            }
        }
        return nullptr;
    }

    // Finds a structural LevelMeterPlugin by its stored EditItemID within a plugin list, or null.
    [[nodiscard]] static tracktion::LevelMeterPlugin* findLevelMeter(
        tracktion::PluginList& list, tracktion::EditItemID plugin_id)
    {
        if (!plugin_id.isValid())
        {
            return nullptr;
        }
        for (tracktion::Plugin* const plugin : list)
        {
            if (plugin != nullptr && plugin->itemID == plugin_id)
            {
                return dynamic_cast<tracktion::LevelMeterPlugin*>(plugin);
            }
        }
        return nullptr;
    }

    // Finds the input/output structural LevelMeterPlugin on the instrument track, or null.
    [[nodiscard]] tracktion::LevelMeterPlugin* findStructuralMeterPlugin(
        tracktion::EditItemID plugin_id) const
    {
        tracktion::AudioTrack* const instrument_track = instrumentTrack();
        if (instrument_track == nullptr)
        {
            return nullptr;
        }
        return findLevelMeter(instrument_track->pluginList, plugin_id);
    }

    // Finds the master-output structural LevelMeterPlugin on the edit master plugin list, or null.
    [[nodiscard]] tracktion::LevelMeterPlugin* findStructuralMasterMeterPlugin(
        tracktion::EditItemID plugin_id) const
    {
        if (m_edit == nullptr)
        {
            return nullptr;
        }
        return findLevelMeter(m_edit->getMasterPluginList(), plugin_id);
    }

    // Creates a hidden live-rig gain plugin on the instrument track at the given index.
    [[nodiscard]] std::expected<LiveRigGainPlugin*, LiveRigError> createLiveRigGainPlugin(
        int insert_index)
    {
        tracktion::AudioTrack* const instrument_track = instrumentTrack();
        if (instrument_track == nullptr)
        {
            return std::unexpected{LiveRigError{LiveRigErrorCode::TrackMissing}};
        }
        const tracktion::Plugin::Ptr plugin =
            m_edit->getPluginCache().createNewPlugin(LiveRigGainPlugin::createState());
        if (plugin == nullptr)
        {
            return std::unexpected{LiveRigError{
                LiveRigErrorCode::PluginRestoreFailed,
                "Could not create structural live rig gain plugin",
            }};
        }
        instrument_track->pluginList.insertPlugin(plugin, insert_index, nullptr);
        auto* const live_rig_gain = dynamic_cast<LiveRigGainPlugin*>(plugin.get());
        if (live_rig_gain == nullptr || !instrument_track->pluginList.contains(live_rig_gain))
        {
            return std::unexpected{LiveRigError{
                LiveRigErrorCode::PluginRestoreFailed,
                "Could not insert structural live rig gain plugin",
            }};
        }

        return live_rig_gain;
    }

    // Creates and inserts a hidden structural LevelMeterPlugin at a slot in the given plugin list.
    [[nodiscard]] std::expected<tracktion::LevelMeterPlugin*, LiveRigError> createLevelMeter(
        tracktion::PluginList& list, int insert_index)
    {
        const tracktion::Plugin::Ptr plugin =
            m_edit->getPluginCache().createNewPlugin(tracktion::LevelMeterPlugin::xmlTypeName, {});
        if (plugin == nullptr)
        {
            return std::unexpected{LiveRigError{
                LiveRigErrorCode::PluginRestoreFailed,
                "Could not create structural meter plugin",
            }};
        }
        list.insertPlugin(plugin, insert_index, nullptr);
        auto* const level_meter = dynamic_cast<tracktion::LevelMeterPlugin*>(plugin.get());
        if (level_meter == nullptr || !list.contains(level_meter))
        {
            return std::unexpected{LiveRigError{
                LiveRigErrorCode::PluginRestoreFailed,
                "Could not insert structural meter plugin",
            }};
        }

        return level_meter;
    }

    // Creates the input/output LevelMeterPlugin on the instrument track at the given slot.
    [[nodiscard]] std::expected<tracktion::LevelMeterPlugin*, LiveRigError> createLevelMeterPlugin(
        int insert_index)
    {
        tracktion::AudioTrack* const instrument_track = instrumentTrack();
        if (instrument_track == nullptr)
        {
            return std::unexpected{LiveRigError{LiveRigErrorCode::TrackMissing}};
        }
        return createLevelMeter(instrument_track->pluginList, insert_index);
    }

    // Creates the master-output LevelMeterPlugin at the end of the edit master plugin list. Unlike
    // EditPlaybackContext::masterLevels, this measurer is not torn down when a plugin reconfigure
    // rebuilds the playback graph, so the UI meter read stays a no-op re-attach.
    [[nodiscard]] std::expected<tracktion::LevelMeterPlugin*, LiveRigError>
    createMasterLevelMeterPlugin()
    {
        if (m_edit == nullptr)
        {
            return std::unexpected{LiveRigError{
                LiveRigErrorCode::PluginRestoreFailed,
                "Edit is not available for master meter creation",
            }};
        }
        return createLevelMeter(m_edit->getMasterPluginList(), -1);
    }

    // Attaches a meter reader to a level meter's measurer, or detaches it when the meter is absent.
    static void attachMeterReader(MeterReader& reader, tracktion::LevelMeterPlugin* meter)
    {
        if (meter != nullptr)
        {
            reader.attach(&meter->measurer);
        }
        else
        {
            reader.detach();
        }
    }

    // Detaches a meter reader and clears the retained peak window on its plugin's measurer.
    static void detachAndClearMeter(MeterReader& reader, tracktion::LevelMeterPlugin* meter)
    {
        reader.detach();
        if (meter != nullptr)
        {
            meter->measurer.clear();
        }
    }

    // Validates that the hidden live-rig plugins exist at their fixed measurement points.
    [[nodiscard]] std::expected<void, LiveRigError> validateStructuralLiveRigPlugins() const
    {
        const tracktion::AudioTrack* const instrument_track = instrumentTrack();
        if (instrument_track == nullptr)
        {
            return std::unexpected{LiveRigError{LiveRigErrorCode::TrackMissing}};
        }

        const auto* const input_plugin = findStructuralGainPlugin(m_input_gain_plugin_id);
        const auto* const input_meter = findStructuralMeterPlugin(m_input_meter_plugin_id);
        const auto* const output_plugin = findStructuralGainPlugin(m_output_gain_plugin_id);
        const auto* const output_meter = findStructuralMeterPlugin(m_output_meter_plugin_id);
        if (input_plugin == nullptr || input_meter == nullptr || output_plugin == nullptr ||
            output_meter == nullptr)
        {
            return std::unexpected{LiveRigError{
                LiveRigErrorCode::PluginRestoreFailed,
                "Structural live rig plugins are missing",
            }};
        }

        const auto& plugin_list = instrument_track->pluginList.getPlugins();
        if (plugin_list.size() < 4 || plugin_list[0] == nullptr || plugin_list[1] == nullptr)
        {
            return std::unexpected{LiveRigError{
                LiveRigErrorCode::PluginRestoreFailed,
                "Structural live rig plugins are not in fixed slots",
            }};
        }
        if (plugin_list.getLast() == nullptr || plugin_list[plugin_list.size() - 2] == nullptr)
        {
            return std::unexpected{LiveRigError{
                LiveRigErrorCode::PluginRestoreFailed,
                "Structural live rig plugins are not in fixed slots",
            }};
        }

        if (plugin_list[0]->itemID != m_input_gain_plugin_id ||
            plugin_list[1]->itemID != m_input_meter_plugin_id ||
            plugin_list[plugin_list.size() - 2]->itemID != m_output_gain_plugin_id ||
            plugin_list.getLast()->itemID != m_output_meter_plugin_id)
        {
            return std::unexpected{LiveRigError{
                LiveRigErrorCode::PluginRestoreFailed,
                "Structural live rig plugins are not in fixed slots",
            }};
        }

        return {};
    }

    // Creates the fixed hidden gain and meter plugins around the user-visible live-rig chain.
    [[nodiscard]] std::expected<void, LiveRigError> createStructuralLiveRigPlugins()
    {
        if (instrumentTrack() == nullptr)
        {
            return std::unexpected{LiveRigError{LiveRigErrorCode::TrackMissing}};
        }

        auto created_input_plugin = createLiveRigGainPlugin(0);
        if (!created_input_plugin.has_value())
        {
            return std::unexpected{std::move(created_input_plugin.error())};
        }
        m_input_gain_plugin_id = (*created_input_plugin)->itemID;

        auto created_input_meter = createLevelMeterPlugin(1);
        if (!created_input_meter.has_value())
        {
            return std::unexpected{std::move(created_input_meter.error())};
        }
        m_input_meter_plugin_id = (*created_input_meter)->itemID;

        auto created_output_plugin = createLiveRigGainPlugin(-1);
        if (!created_output_plugin.has_value())
        {
            return std::unexpected{std::move(created_output_plugin.error())};
        }
        m_output_gain_plugin_id = (*created_output_plugin)->itemID;

        auto created_output_meter = createLevelMeterPlugin(-1);
        if (!created_output_meter.has_value())
        {
            return std::unexpected{std::move(created_output_meter.error())};
        }
        m_output_meter_plugin_id = (*created_output_meter)->itemID;

        auto created_master_meter = createMasterLevelMeterPlugin();
        if (!created_master_meter.has_value())
        {
            return std::unexpected{std::move(created_master_meter.error())};
        }
        m_master_meter_plugin_id = (*created_master_meter)->itemID;

        return validateStructuralLiveRigPlugins();
    }

    // Removes only user-visible plugins, preserving fixed structural gain and meter anchors.
    [[nodiscard]] std::expected<void, LiveRigError> clearUserLiveRigPlugins()
    {
        tracktion::AudioTrack* const instrument_track = instrumentTrack();
        if (instrument_track == nullptr)
        {
            return std::unexpected{LiveRigError{LiveRigErrorCode::TrackMissing}};
        }

        clearPluginParameterObservers();
        const tracktion::Plugin::Array plugins = instrument_track->pluginList.getPlugins();
        for (tracktion::Plugin* const plugin : plugins)
        {
            if (plugin != nullptr && !isStructuralLiveRigPlugin(plugin))
            {
                plugin->removeFromParent();
            }
        }

        return validateStructuralLiveRigPlugins();
    }

    // Clears live-rig meter windows retained by structural meter plugins across project changes.
    void clearRetainedLiveRigMeterState()
    {
        detachAndClearMeter(
            m_input_meter_reader, findStructuralMeterPlugin(m_input_meter_plugin_id));
        detachAndClearMeter(
            m_output_meter_reader, findStructuralMeterPlugin(m_output_meter_plugin_id));
        detachAndClearMeter(
            m_master_meter_reader, findStructuralMasterMeterPlugin(m_master_meter_plugin_id));
    }

    // Resets project-owned live-rig structural state while preserving device input calibration.
    [[nodiscard]] std::expected<void, LiveRigError> resetLiveRigProjectState()
    {
        auto output_reset = applyGainToPlugin(m_output_gain_plugin_id, Gain{defaultGainDb()});
        if (!output_reset.has_value())
        {
            return std::unexpected{std::move(output_reset.error())};
        }

        clearRetainedLiveRigMeterState();
        return {};
    }

    // Reads the dB value from a structural live-rig gain plugin, returning default if absent.
    [[nodiscard]] Gain readGainFromPlugin(tracktion::EditItemID plugin_id) const
    {
        const auto* const plugin = findStructuralGainPlugin(plugin_id);
        if (plugin == nullptr)
        {
            return Gain{};
        }
        return plugin->gain();
    }

    // Applies a gain value to a structural live-rig gain plugin.
    [[nodiscard]] std::expected<void, LiveRigError> applyGainToPlugin(
        tracktion::EditItemID plugin_id, Gain gain)
    {
        auto* const plugin = findStructuralGainPlugin(plugin_id);
        if (plugin == nullptr)
        {
            return std::unexpected{LiveRigError{
                LiveRigErrorCode::PluginRestoreFailed,
                "Structural live rig gain plugin is missing",
            }};
        }

        plugin->setGain(gain);
        return {};
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
        m_input_meter_reader.detach();
        m_output_meter_reader.detach();
        m_master_meter_reader.detach();
        transport.stop(discard_recordings, clear_devices);
        transport.freePlaybackContext();
    }

    // Removes live input assignments from Rock Hero's monitoring target tracks.
    void clearInstrumentInputAssignments()
    {
        if (tracktion::AudioTrack* const backing_track = backingTrack(); backing_track != nullptr)
        {
            m_edit->getEditInputDevices().clearAllInputs(*backing_track, nullptr);
        }

        if (tracktion::AudioTrack* const instrument_track = instrumentTrack();
            instrument_track != nullptr)
        {
            m_edit->getEditInputDevices().clearAllInputs(*instrument_track, nullptr);
        }
    }

    // Removes stale monitoring assignments from Tracktion's active input instances.
    void detachInstrumentMonitoringRoute()
    {
        auto& transport = m_edit->getTransport();
        const bool should_release_context = !transport.isPlaying();
        if (should_release_context && m_edit->getCurrentPlaybackContext() == nullptr)
        {
            // clearAllInputs enumerates only the current playback context. Allocate a stopped
            // context long enough to remove persisted targets, then release it below.
            transport.ensureContextAllocated(true);
        }

        clearInstrumentInputAssignments();
        m_raw_input_meter_reader.detach();

        if (should_release_context)
        {
            m_input_meter_reader.detach();
            m_output_meter_reader.detach();
            m_master_meter_reader.detach();
            transport.freePlaybackContext();
        }
    }

    // Detaches any previous route before surfacing why the new route cannot be armed.
    [[nodiscard]] LiveInputError failInstrumentMonitoringRoute(const juce::String& reason)
    {
        detachInstrumentMonitoringRoute();
        return liveInputRouteUnavailable(reason);
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

    // Finds the current mono instrument input device for raw route metering.
    [[nodiscard]] tracktion::WaveInputDevice* currentInstrumentWaveInput() const
    {
        const tracktion::DeviceManager& tracktion_device_manager = m_engine->getDeviceManager();
        juce::AudioIODevice* const current_device =
            tracktion_device_manager.deviceManager.getCurrentAudioDevice();
        if (current_device == nullptr)
        {
            return nullptr;
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
            return nullptr;
        }

        return findInstrumentWaveInput(wave_descriptions->input);
    }

    // Binds the selected app-local mono input to the active Tracktion monitoring target.
    std::expected<void, LiveInputError> applyInstrumentMonitoringRoute()
    {
        if (!juce::MessageManager::getInstance()->isThisTheMessageThread())
        {
            return std::unexpected{LiveInputError{LiveInputErrorCode::MessageThreadRequired}};
        }

        if (!m_live_input_monitoring_enabled && !m_calibration_input_monitoring_enabled)
        {
            detachInstrumentMonitoringRoute();
            return {};
        }

        const tracktion::AudioTrack* const monitoring_target =
            m_calibration_input_monitoring_enabled ? backingTrack() : instrumentTrack();
        if (monitoring_target == nullptr)
        {
            return std::unexpected{liveInputRouteUnavailable(
                m_calibration_input_monitoring_enabled ? "backing track is missing"
                                                       : "instrument track is missing")};
        }

        tracktion::DeviceManager& tracktion_device_manager = m_engine->getDeviceManager();
        juce::AudioIODevice* const current_device =
            tracktion_device_manager.deviceManager.getCurrentAudioDevice();
        if (current_device == nullptr)
        {
            return std::unexpected{failInstrumentMonitoringRoute("no current audio device")};
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
            return std::unexpected{failInstrumentMonitoringRoute(
                "selected route is not one mono input and one stereo output pair")};
        }

        tracktion_device_manager.dispatchPendingUpdates();

        tracktion::WaveInputDevice* const wave_input =
            findInstrumentWaveInput(wave_descriptions->input);
        if (wave_input == nullptr)
        {
            return std::unexpected{failInstrumentMonitoringRoute(
                "selected mono input is not available to Tracktion")};
        }

        clearInstrumentInputAssignments();

        auto& transport = m_edit->getTransport();
        transport.ensureContextAllocated(true);
        wave_input->setStereoPair(false);

        tracktion::InputDeviceInstance* const input_instance =
            m_edit->getCurrentInstanceForInputDevice(wave_input);
        if (input_instance == nullptr)
        {
            transport.ensureContextAllocated(true);
            return std::unexpected{liveInputRouteUnavailable(
                "selected mono input has no playback instance")};
        }

        const auto target_result = input_instance->setTarget(
            monitoring_target->itemID, true, nullptr, std::optional<int>{0});
        if (!target_result)
        {
            transport.ensureContextAllocated(true);
            return std::unexpected{liveInputRouteUnavailable(
                "could not assign live input to monitoring track: " + target_result.error())};
        }

        input_instance->setRecordingEnabled(monitoring_target->itemID, false);
        wave_input->setMonitorMode(
            (m_live_input_monitoring_enabled || m_calibration_input_monitoring_enabled)
                ? tracktion::InputDevice::MonitorMode::on
                : tracktion::InputDevice::MonitorMode::off);
        transport.ensureContextAllocated(true);
        return {};
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
    std::expected<void, LiveInputError> rebuildInstrumentMonitoringGraph()
    {
        auto route_result = applyInstrumentMonitoringRoute();
        updateTransportState();
        return route_result;
    }

    // Cleanup and rollback paths cannot replace their primary failure with monitoring cleanup
    // detail, so route failures are logged through this named best-effort helper.
    void rebuildInstrumentMonitoringGraphBestEffort(std::string_view context)
    {
        auto route_result = rebuildInstrumentMonitoringGraph();
        if (!route_result.has_value())
        {
            logInstrumentMonitoringFailure(
                toJuceString(context) + ": " + toJuceString(route_result.error().message));
        }
    }

    // Centralizes the shared "adopt the requested monitoring flags, reroute, and roll back to off
    // on route failure" path for the two monitoring toggles, which were otherwise identical apart
    // from the channel and the rollback context. The mutual-exclusion and no-input-device policy
    // lives in the pure, device-free monitoringFlagsForRequest(); this method owns only the side
    // effects. Keeps the existing synchronous LiveInputError contract; callers supply device
    // availability because that check lives on Engine, not Impl.
    [[nodiscard]] std::expected<void, LiveInputError> setMonitoringChannelEnabled(
        MonitorChannel channel, bool enabled, bool input_device_available,
        std::string_view rollback_context)
    {
        const std::optional<MonitoringFlags> requested = monitoringFlagsForRequest(
            MonitoringFlags{
                .live_input = m_live_input_monitoring_enabled,
                .calibration = m_calibration_input_monitoring_enabled
            },
            channel,
            enabled,
            input_device_available);

        if (!requested.has_value())
        {
            // No input device to route from: force both modes off and report the route failure.
            m_live_input_monitoring_enabled = false;
            m_calibration_input_monitoring_enabled = false;
            rebuildInstrumentMonitoringGraphBestEffort(rollback_context);
            return std::unexpected{LiveInputError{LiveInputErrorCode::InputRouteUnavailable}};
        }

        m_live_input_monitoring_enabled = requested->live_input;
        m_calibration_input_monitoring_enabled = requested->calibration;

        auto route_result = rebuildInstrumentMonitoringGraph();
        if (!route_result.has_value())
        {
            LiveInputError route_error = std::move(route_result.error());
            if (enabled)
            {
                m_live_input_monitoring_enabled = false;
                m_calibration_input_monitoring_enabled = false;
                rebuildInstrumentMonitoringGraphBestEffort(rollback_context);
            }
            return std::unexpected{std::move(route_error)};
        }

        return {};
    }

    // Connects meter readers to their structural measurers and returns one display snapshot. All
    // three meters ride stable structural LevelMeterPlugins (the master deliberately does not use the
    // churning EditPlaybackContext::masterLevels), so each attach() is a no-op once registered and the
    // read never re-registers a client onto a measurer a plugin reconfigure is mid-rebuild.
    [[nodiscard]] AudioMeterSnapshot audioMeterSnapshot() const
    {
        attachMeterReader(m_input_meter_reader, findStructuralMeterPlugin(m_input_meter_plugin_id));
        attachMeterReader(
            m_output_meter_reader, findStructuralMeterPlugin(m_output_meter_plugin_id));
        attachMeterReader(
            m_master_meter_reader, findStructuralMasterMeterPlugin(m_master_meter_plugin_id));

        return AudioMeterSnapshot{
            .live_rig_input = m_input_meter_reader.read(),
            .live_rig_output = m_output_meter_reader.read(),
            .master_output = m_master_meter_reader.read(),
        };
    }

    // Reads the hardware input meter before the live-rig monitoring gate.
    [[nodiscard]] AudioMeterLevel rawInputMeterLevel() const
    {
        if (m_audio_device_configuration_refresh_pending)
        {
            return {};
        }

        if (tracktion::WaveInputDevice* const wave_input = currentInstrumentWaveInput();
            wave_input != nullptr)
        {
            m_raw_input_meter_reader.attach(&wave_input->levelMeasurer);
        }
        else
        {
            m_raw_input_meter_reader.detach();
        }

        return m_raw_input_meter_reader.read();
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
    // Tracktion uses the engine application name as its property-storage folder.
    Impl* const impl = m_impl.get();
    m_impl->m_engine = std::make_unique<tracktion::Engine>(
        toJuceString(core::applicationDataFolderName()),
        std::make_unique<RockHeroUIBehaviour>(
            [impl](PluginWindowCommand command) { impl->dispatchPluginWindowCommand(command); }),
        std::make_unique<RockHeroEngineBehaviour>());
    m_impl->m_engine->getPluginManager().setUsesSeparateProcessForScanning(true);

    // createSingleTrackEdit already provides one AudioTrack ready for media.
    m_impl->createEdit();

    // Start with one instrument input and stereo output; the dialog can reconfigure either at
    // runtime.
    m_impl->m_engine->getDeviceManager().initialise(1, 2);
    m_impl->rebuildInstrumentMonitoringGraphBestEffort("initial monitoring route setup failed");

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
    m_impl->m_alive.reset();
    m_impl->m_load_op.reset();

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
std::expected<void, SongAudioError> Engine::prepareSong(common::core::Song& song)
{
    for (common::core::Arrangement& arrangement : song.arrangements)
    {
        if (arrangement.audio_asset.path.empty())
        {
            return std::unexpected{SongAudioError{
                SongAudioErrorCode::MissingAudioAssetPath,
                "Arrangement is missing a backing audio asset path: " + arrangement.id
            }};
        }

        const auto audio_duration = readAudioDuration(*m_impl->m_engine, arrangement.audio_asset);
        if (!audio_duration.has_value())
        {
            return std::unexpected{audio_duration.error()};
        }

        arrangement.audio_duration = *audio_duration;
    }

    return {};
}

// Makes the prepared arrangement active on the Tracktion backing audio track.
std::expected<void, SongAudioError> Engine::setActiveArrangement(
    const common::core::Arrangement& arrangement)
{
    auto* track = m_impl->backingTrack();
    if (track == nullptr)
    {
        return std::unexpected{SongAudioError{SongAudioErrorCode::MissingBackingTrack}};
    }

    if (arrangement.audio_asset.path.empty())
    {
        return std::unexpected{SongAudioError{
            SongAudioErrorCode::MissingAudioAssetPath,
            "Arrangement is missing a backing audio asset path: " + arrangement.id
        }};
    }

    if (arrangement.audio_duration.seconds <= 0.0)
    {
        return std::unexpected{SongAudioError{
            SongAudioErrorCode::InvalidAudioDuration,
            "Arrangement has no accepted backing audio duration: " + arrangement.id
        }};
    }

    const juce::File file = common::core::juceFileFromPath(arrangement.audio_asset.path);
    if (!file.existsAsFile())
    {
        return std::unexpected{SongAudioError{
            SongAudioErrorCode::UnreadableAudioFile,
            "Backing audio file does not exist: " + pathToUtf8String(arrangement.audio_asset.path)
        }};
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
        m_impl->rebuildInstrumentMonitoringGraphBestEffort(
            "backing clip insertion rollback failed");
        m_impl->updateTransportState();
        return std::unexpected{SongAudioError{
            SongAudioErrorCode::BackendClipInsertionFailed,
            "Could not insert backing audio clip: " + pathToUtf8String(arrangement.audio_asset.path)
        }};
    }

    // Apply persisted normalization gain so playback volume matches the analyzed loudness target.
    if (arrangement.audio_asset.normalization.has_value())
    {
        wave_clip->setGainDB(static_cast<float>(arrangement.audio_asset.normalization->gain_db));
    }

    m_impl->m_loaded_length_seconds = arrangement.audio_duration.seconds;
    transport.looping = false;
    transport.setPosition(tracktion::TimePosition{});
    auto route_result = m_impl->rebuildInstrumentMonitoringGraph();
    if (!route_result.has_value())
    {
        return std::unexpected{songAudioErrorFromLiveInputError(route_result.error())};
    }
    m_impl->updateTransportState();
    return {};
}

// Clears the backing track so closed projects do not leave stale media in Tracktion.
std::expected<void, SongAudioError> Engine::clearActiveArrangement()
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
    auto route_result = m_impl->rebuildInstrumentMonitoringGraph();
    if (!route_result.has_value())
    {
        return std::unexpected{songAudioErrorFromLiveInputError(route_result.error())};
    }
    m_impl->updateTransportState();
    return {};
}

// Scans JUCE's default VST3 roots through Tracktion's known-plugin list. Tracktion persists the
// resulting descriptions, so repeated scans can reuse unchanged entries.
std::expected<void, PluginHostError> Engine::scanPluginCatalog(
    PluginCatalogScanProgressCallback progress_callback,
    const common::core::CancellationToken& cancel)
{
    juce::AudioPluginFormat* const format = m_impl->vst3PluginFormat();
    if (format == nullptr)
    {
        return std::unexpected{PluginHostError{
            PluginHostErrorCode::PluginScanFailed,
            "VST3 plugin hosting is not enabled in this build"
        }};
    }

    auto scanned_files = m_impl->scanVst3SearchPath(
        format->getDefaultLocationsToSearch(), progress_callback, cancel);
    if (!scanned_files.has_value())
    {
        return std::unexpected{std::move(scanned_files.error())};
    }

    return {};
}

// Scans user-supplied VST3 locations through Tracktion's known-plugin list.
std::expected<std::vector<PluginCandidate>, PluginHostError> Engine::scanPluginLocations(
    const std::vector<std::filesystem::path>& roots,
    PluginCatalogScanProgressCallback progress_callback)
{
    auto scanned_files =
        m_impl->scanVst3SearchPath(Impl::pluginSearchPathFromRoots(roots), progress_callback);
    if (!scanned_files.has_value())
    {
        return std::unexpected{std::move(scanned_files.error())};
    }

    return m_impl->knownPluginCatalogForScannedFiles(*scanned_files);
}

// Reads Tracktion's known-plugin list without launching plugin scanners.
std::vector<PluginCandidate> Engine::knownPluginCatalog() const
{
    return m_impl->knownPluginCatalog();
}

// Inserts a selected VST3 candidate into the instrument track's user-visible plugin chain.
std::expected<PluginInsertResult, PluginHostError> Engine::Impl::insertPluginCandidateToTrack(
    const PluginCandidate& plugin_candidate, std::size_t chain_index)
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

    if (!instrument_track->pluginList.canInsertPlugin())
    {
        return std::unexpected{PluginHostError{PluginHostErrorCode::PluginInsertionFailed}};
    }

    const std::size_t plugin_count = userVisiblePluginCount();
    if (chain_index > plugin_count)
    {
        return std::unexpected{PluginHostError{PluginHostErrorCode::InvalidChainIndex}};
    }

    if (plugin_count >= max_signal_chain_plugins)
    {
        return std::unexpected{PluginHostError{
            PluginHostErrorCode::PluginChainLimitExceeded,
            pluginChainLimitExceededMessage(plugin_count),
        }};
    }

    auto insert_position = tracktionIndexForUserPluginSlot(chain_index);
    if (!insert_position.has_value())
    {
        return std::unexpected{std::move(insert_position.error())};
    }

    std::unique_ptr<juce::PluginDescription> description = findKnownPlugin(plugin_candidate.id);
    std::string resolved_plugin_id = plugin_candidate.id;
    if (description == nullptr)
    {
        if (plugin_candidate.file_path.empty())
        {
            return std::unexpected{PluginHostError{
                PluginHostErrorCode::PluginNotFound,
                "Plugin candidate was not found: " + plugin_candidate.id
            }};
        }

        const auto validation_started_at = std::chrono::steady_clock::now();
        const auto scan_result = scanPluginFileForCandidates(plugin_candidate.file_path);
        logPluginValidationSummary(
            plugin_candidate.file_path,
            elapsedMilliseconds(validation_started_at),
            scan_result.has_value() ? std::optional<std::string>{}
                                    : std::optional<std::string>{scan_result.error().message});
        if (!scan_result.has_value())
        {
            return std::unexpected{scan_result.error()};
        }

        resolved_plugin_id = scan_result->front().id;
        description = findKnownPlugin(resolved_plugin_id);
        if (description == nullptr)
        {
            return std::unexpected{PluginHostError{
                PluginHostErrorCode::PluginNotFound,
                "Plugin candidate was not found after scanning: " +
                    pathToUtf8String(plugin_candidate.file_path)
            }};
        }
    }
    tracktion::Plugin::Ptr plugin;
    auto mutation_result = mutateAndReroutePluginChain(
        [this, &description, &instrument_track, &insert_position, &plugin]
        -> std::expected<void, PluginChainMutationFailure> {
            plugin = m_edit->getPluginCache().createNewPlugin(
                tracktion::ExternalPlugin::xmlTypeName, *description);
            if (plugin == nullptr)
            {
                return std::unexpected{PluginChainMutationFailure{
                    PluginHostError{
                        PluginHostErrorCode::PluginCreationFailed,
                        "Could not create plugin: " + description->name.toStdString(),
                    },
                    "plugin creation rollback failed",
                }};
            }

            if (auto* const external_plugin =
                    dynamic_cast<tracktion::ExternalPlugin*>(plugin.get());
                external_plugin != nullptr)
            {
                const juce::String load_error = external_plugin->getLoadError();
                if (load_error.isNotEmpty())
                {
                    return std::unexpected{PluginChainMutationFailure{
                        PluginHostError{
                            PluginHostErrorCode::PluginLoadFailed,
                            load_error.toStdString(),
                        },
                        "plugin load rollback failed",
                    }};
                }
            }

            instrument_track->pluginList.insertPlugin(plugin, *insert_position, nullptr);
            if (instrument_track->pluginList.indexOf(plugin.get()) < 0)
            {
                return std::unexpected{PluginChainMutationFailure{
                    PluginHostError{PluginHostErrorCode::PluginInsertionFailed},
                    "plugin insertion rollback failed",
                }};
            }

            return {};
        },
        [&plugin] {
            if (plugin != nullptr)
            {
                plugin->deleteFromParent();
            }
        },
        "plugin insertion route rollback failed");
    if (!mutation_result.has_value())
    {
        return std::unexpected{std::move(mutation_result.error())};
    }

    auto snapshot = pluginChainSnapshot();
    for (PluginChainEntry& entry : snapshot.plugins)
    {
        if (entry.instance_id == plugin->itemID.toString().toStdString())
        {
            entry.plugin_id = resolved_plugin_id;
            break;
        }
    }

    return PluginInsertResult{
        .snapshot = std::move(snapshot),
        .inserted_instance_id = plugin->itemID.toString().toStdString(),
    };
}

// Inserts a selected VST3 candidate into the instrument track's user-visible plugin chain.
std::expected<PluginInsertResult, PluginHostError> Engine::insertPlugin(
    const PluginCandidate& plugin_candidate, std::size_t chain_index)
{
    return m_impl->insertPluginCandidateToTrack(plugin_candidate, chain_index);
}

// Moves a loaded plugin inside the instrument track and rebuilds monitoring around the mutation.
std::expected<PluginChainSnapshot, PluginHostError> Engine::movePlugin(
    const std::string& instance_id, std::size_t destination_index)
{
    if (!juce::MessageManager::getInstance()->isThisTheMessageThread())
    {
        return std::unexpected{PluginHostError{PluginHostErrorCode::MessageThreadRequired}};
    }

    tracktion::AudioTrack* const instrument_track = m_impl->instrumentTrack();
    if (instrument_track == nullptr)
    {
        return std::unexpected{PluginHostError{PluginHostErrorCode::TrackMissing}};
    }

    tracktion::Plugin* const plugin = m_impl->findInstrumentPluginInstance(instance_id);
    if (plugin == nullptr || m_impl->isStructuralLiveRigPlugin(plugin))
    {
        return std::unexpected{PluginHostError{
            PluginHostErrorCode::PluginInstanceNotFound,
            "Plugin instance was not found: " + instance_id
        }};
    }

    const std::size_t plugin_count = m_impl->userVisiblePluginCount();
    if (destination_index >= plugin_count)
    {
        return std::unexpected{PluginHostError{PluginHostErrorCode::InvalidChainIndex}};
    }

    const std::optional<std::size_t> current_index = m_impl->userVisiblePluginIndexOf(plugin);
    if (!current_index.has_value())
    {
        return std::unexpected{PluginHostError{
            PluginHostErrorCode::PluginInstanceNotFound,
            "Plugin instance was not found: " + instance_id
        }};
    }

    if (*current_index == destination_index)
    {
        return m_impl->pluginChainSnapshot();
    }

    const int original_tracktion_index = instrument_track->pluginList.indexOf(plugin);
    auto destination_tracktion_index =
        m_impl->tracktionIndexForUserPluginSlot(destination_index, plugin);
    if (!destination_tracktion_index.has_value())
    {
        return std::unexpected{std::move(destination_tracktion_index.error())};
    }

    const tracktion::Plugin::Ptr moved_plugin{plugin};
    const auto rollback_move =
        [this, &instrument_track, &moved_plugin, current_index, original_tracktion_index] {
            auto rollback_tracktion_index =
                m_impl->tracktionIndexForUserPluginSlot(*current_index, moved_plugin.get());
            instrument_track->pluginList.insertPlugin(
                moved_plugin,
                rollback_tracktion_index.has_value() ? *rollback_tracktion_index
                                                     : original_tracktion_index,
                nullptr);
        };

    const int move_insert_index = tracktionInsertionIndexForExistingPluginMove(
        original_tracktion_index, *destination_tracktion_index);
    auto mutation_result = m_impl->mutateAndReroutePluginChain(
        [&instrument_track,
         moved_plugin,
         move_insert_index,
         plugin,
         destination_index,
         &rollback_move,
         this] -> std::expected<void, PluginChainMutationFailure> {
            instrument_track->pluginList.insertPlugin(moved_plugin, move_insert_index, nullptr);
            if (instrument_track->pluginList.indexOf(plugin) < 0 ||
                m_impl->userVisiblePluginIndexOf(plugin) !=
                    std::optional<std::size_t>{destination_index})
            {
                rollback_move();
                return std::unexpected{PluginChainMutationFailure{
                    PluginHostError{PluginHostErrorCode::PluginMoveFailed},
                    "plugin move rollback failed",
                }};
            }

            return {};
        },
        rollback_move,
        "plugin move route rollback failed");
    if (!mutation_result.has_value())
    {
        return std::unexpected{std::move(mutation_result.error())};
    }

    return m_impl->pluginChainSnapshot();
}

// Removes a loaded plugin from the instrument track and rebuilds monitoring around the mutation.
std::expected<PluginChainSnapshot, PluginHostError> Engine::removePlugin(
    const std::string& instance_id)
{
    if (!juce::MessageManager::getInstance()->isThisTheMessageThread())
    {
        return std::unexpected{PluginHostError{PluginHostErrorCode::MessageThreadRequired}};
    }

    tracktion::AudioTrack* const instrument_track = m_impl->instrumentTrack();
    if (instrument_track == nullptr)
    {
        return std::unexpected{PluginHostError{PluginHostErrorCode::TrackMissing}};
    }

    tracktion::Plugin* const plugin = m_impl->findInstrumentPluginInstance(instance_id);
    if (plugin == nullptr || m_impl->isStructuralLiveRigPlugin(plugin))
    {
        return std::unexpected{PluginHostError{
            PluginHostErrorCode::PluginInstanceNotFound,
            "Plugin instance was not found: " + instance_id
        }};
    }

    const int original_tracktion_index = instrument_track->pluginList.indexOf(plugin);
    const tracktion::Plugin::Ptr removed_plugin{plugin};
    auto mutation_result = m_impl->mutateAndReroutePluginChain(
        [&instrument_track, plugin] -> std::expected<void, PluginChainMutationFailure> {
            plugin->removeFromParent();
            if (instrument_track->pluginList.indexOf(plugin) >= 0)
            {
                return std::unexpected{PluginChainMutationFailure{
                    PluginHostError{PluginHostErrorCode::PluginRemovalFailed},
                    "plugin removal rollback failed",
                }};
            }

            return {};
        },
        [&instrument_track, removed_plugin, original_tracktion_index] {
            instrument_track->pluginList.insertPlugin(
                removed_plugin, original_tracktion_index, nullptr);
        },
        "plugin removal route rollback failed");
    if (!mutation_result.has_value())
    {
        return std::unexpected{std::move(mutation_result.error())};
    }
    m_impl->commitPluginRemoval(*removed_plugin);
    return m_impl->pluginChainSnapshot();
}

// Captures a user plugin's Tracktion state into an opaque editor-core memento.
std::expected<PluginInstanceState, PluginHostError> Engine::capturePluginState(
    const std::string& instance_id)
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
    if (plugin == nullptr || m_impl->isStructuralLiveRigPlugin(plugin))
    {
        return std::unexpected{PluginHostError{
            PluginHostErrorCode::PluginInstanceNotFound,
            "Plugin instance was not found: " + instance_id,
        }};
    }

    auto* const external_plugin = dynamic_cast<tracktion::ExternalPlugin*>(plugin);
    if (external_plugin == nullptr)
    {
        return std::unexpected{PluginHostError{
            PluginHostErrorCode::PluginStateCaptureFailed,
            "Only external plugins can be captured right now: " + plugin->getName().toStdString(),
        }};
    }

    external_plugin->flushPluginStateToValueTree();
    return makePluginInstanceState(external_plugin->state.createCopy());
}

// Recreates a captured external-plugin memento as a user-visible plugin with its original id.
std::expected<PluginChainSnapshot, PluginHostError> Engine::recreatePluginStatePreservingId(
    const PluginInstanceState& state, std::size_t chain_index)
{
    if (!juce::MessageManager::getInstance()->isThisTheMessageThread())
    {
        return std::unexpected{PluginHostError{PluginHostErrorCode::MessageThreadRequired}};
    }

    auto plugin_state = pluginStateTreeFromMemento(state);
    if (!plugin_state.has_value())
    {
        return std::unexpected{std::move(plugin_state.error())};
    }

    tracktion::AudioTrack* const instrument_track = m_impl->instrumentTrack();
    if (instrument_track == nullptr)
    {
        return std::unexpected{PluginHostError{PluginHostErrorCode::TrackMissing}};
    }

    if (!instrument_track->pluginList.canInsertPlugin())
    {
        return std::unexpected{PluginHostError{PluginHostErrorCode::PluginInsertionFailed}};
    }

    const std::size_t plugin_count = m_impl->userVisiblePluginCount();
    if (chain_index > plugin_count)
    {
        return std::unexpected{PluginHostError{PluginHostErrorCode::InvalidChainIndex}};
    }

    if (plugin_count >= max_signal_chain_plugins)
    {
        return std::unexpected{PluginHostError{
            PluginHostErrorCode::PluginChainLimitExceeded,
            pluginChainLimitExceededMessage(plugin_count),
        }};
    }

    auto insert_position = m_impl->tracktionIndexForUserPluginSlot(chain_index);
    if (!insert_position.has_value())
    {
        return std::unexpected{std::move(insert_position.error())};
    }

    const std::string original_instance_id = pluginInstanceIdFromState(*plugin_state);
    if (original_instance_id.empty())
    {
        return std::unexpected{PluginHostError{
            PluginHostErrorCode::PluginStateRestoreFailed,
            "Captured plugin state does not contain a runtime instance id",
        }};
    }

    if (m_impl->findInstrumentPluginInstance(original_instance_id) != nullptr)
    {
        return std::unexpected{PluginHostError{
            PluginHostErrorCode::PluginStateRestoreFailed,
            "Plugin instance id is already loaded: " + original_instance_id,
        }};
    }

    tracktion::Plugin::Ptr inserted_plugin;
    auto mutation_result = m_impl->mutateAndReroutePluginChain(
        [&instrument_track,
         &plugin_state,
         &insert_position,
         &inserted_plugin,
         &original_instance_id] -> std::expected<void, PluginChainMutationFailure> {
            auto remove_partial_insert =
                [&instrument_track,
                 &inserted_plugin] -> std::expected<void, PluginChainMutationFailure> {
                if (inserted_plugin != nullptr)
                {
                    tracktion::Plugin* const inserted_plugin_ptr = inserted_plugin.get();
                    inserted_plugin->deleteFromParent();
                    if (instrument_track->pluginList.indexOf(inserted_plugin_ptr) >= 0)
                    {
                        return std::unexpected{PluginChainMutationFailure{
                            .error =
                                PluginHostError{
                                    PluginHostErrorCode::RollbackContractViolation,
                                    "Could not remove partial recreated plugin",
                                },
                            .reroute_context = "plugin-state recreate rollback failed",
                        }};
                    }
                }
                inserted_plugin = nullptr;
                return {};
            };

            inserted_plugin = instrument_track->pluginList.insertPlugin(
                plugin_state->createCopy(), *insert_position);
            auto* const external_plugin =
                inserted_plugin != nullptr
                    ? dynamic_cast<tracktion::ExternalPlugin*>(inserted_plugin.get())
                    : nullptr;
            if (external_plugin == nullptr)
            {
                if (auto removed = remove_partial_insert(); !removed.has_value())
                {
                    return std::unexpected{std::move(removed.error())};
                }
                return std::unexpected{PluginChainMutationFailure{
                    .error =
                        PluginHostError{
                            PluginHostErrorCode::PluginStateRestoreFailed,
                            "Could not recreate captured plugin state",
                        },
                    .reroute_context = "plugin-state recreate rollback failed",
                }};
            }

            const juce::String load_error = external_plugin->getLoadError();
            if (load_error.isNotEmpty())
            {
                if (auto removed = remove_partial_insert(); !removed.has_value())
                {
                    return std::unexpected{std::move(removed.error())};
                }
                return std::unexpected{PluginChainMutationFailure{
                    .error =
                        PluginHostError{
                            PluginHostErrorCode::PluginStateRestoreFailed,
                            load_error.toStdString(),
                        },
                    .reroute_context = "plugin-state recreate load rollback failed",
                }};
            }

            if (instrument_track->pluginList.indexOf(inserted_plugin.get()) < 0)
            {
                if (auto removed = remove_partial_insert(); !removed.has_value())
                {
                    return std::unexpected{std::move(removed.error())};
                }
                return std::unexpected{PluginChainMutationFailure{
                    .error = PluginHostError{PluginHostErrorCode::PluginInsertionFailed},
                    .reroute_context = "plugin-state recreate rollback failed",
                }};
            }

            const std::string restored_instance_id =
                inserted_plugin->itemID.toString().toStdString();
            if (restored_instance_id != original_instance_id)
            {
                if (auto removed = remove_partial_insert(); !removed.has_value())
                {
                    return std::unexpected{std::move(removed.error())};
                }
                return std::unexpected{PluginChainMutationFailure{
                    .error =
                        PluginHostError{
                            PluginHostErrorCode::PluginStateRestoreFailed,
                            "Recreated plugin id did not match captured state",
                        },
                    .reroute_context = "plugin-state recreate id rollback failed",
                }};
            }

            return {};
        },
        [&inserted_plugin] {
            if (inserted_plugin != nullptr)
            {
                inserted_plugin->deleteFromParent();
            }
        },
        "plugin-state recreate route rollback failed");
    if (!mutation_result.has_value())
    {
        return std::unexpected{std::move(mutation_result.error())};
    }

    return m_impl->pluginChainSnapshot();
}

// Restores a captured state chunk into an existing external plugin's live processor.
std::expected<void, PluginHostError> Engine::setPluginState(
    const std::string& instance_id, const PluginInstanceState& state)
{
    if (!juce::MessageManager::getInstance()->isThisTheMessageThread())
    {
        return std::unexpected{PluginHostError{PluginHostErrorCode::MessageThreadRequired}};
    }

    auto plugin_state = pluginStateTreeFromMemento(state);
    if (!plugin_state.has_value())
    {
        return std::unexpected{std::move(plugin_state.error())};
    }

    if (m_impl->instrumentTrack() == nullptr)
    {
        return std::unexpected{PluginHostError{PluginHostErrorCode::TrackMissing}};
    }

    tracktion::Plugin* const plugin = m_impl->findInstrumentPluginInstance(instance_id);
    if (plugin == nullptr || m_impl->isStructuralLiveRigPlugin(plugin))
    {
        return std::unexpected{PluginHostError{
            PluginHostErrorCode::PluginInstanceNotFound,
            "Plugin instance was not found: " + instance_id,
        }};
    }

    auto* const external_plugin = dynamic_cast<tracktion::ExternalPlugin*>(plugin);
    if (external_plugin == nullptr)
    {
        return std::unexpected{PluginHostError{
            PluginHostErrorCode::PluginStateRestoreFailed,
            "Only external plugin states can be restored right now: " +
                plugin->getName().toStdString(),
        }};
    }

    {
        const juce::ScopedValueSetter<bool> suppress_parameter_observation(
            m_impl->m_plugin_parameter_observation_suppressed, true);
        external_plugin->restorePluginStateFromValueTree(*plugin_state);
        copyPluginStatePreservingInstanceId(*external_plugin, *plugin_state);
    }
    m_impl->refreshPluginParameterObservers();

    return {};
}

// Applies one parameter value through Tracktion's parameter object instead of restoring a chunk.
std::expected<void, PluginHostError> Engine::setPluginParameterValue(
    const std::string& instance_id, const std::string& parameter_id, int parameter_index,
    double normalized_value)
{
    if (!juce::MessageManager::getInstance()->isThisTheMessageThread())
    {
        return std::unexpected{PluginHostError{PluginHostErrorCode::MessageThreadRequired}};
    }

    tracktion::Plugin* const plugin = m_impl->findInstrumentPluginInstance(instance_id);
    if (plugin == nullptr || m_impl->isStructuralLiveRigPlugin(plugin))
    {
        return std::unexpected{PluginHostError{
            PluginHostErrorCode::PluginInstanceNotFound,
            "Plugin instance was not found: " + instance_id,
        }};
    }

    auto* const external_plugin = dynamic_cast<tracktion::ExternalPlugin*>(plugin);
    if (external_plugin == nullptr)
    {
        return std::unexpected{PluginHostError{
            PluginHostErrorCode::PluginStateRestoreFailed,
            "Only external plugin parameters can be restored right now: " +
                plugin->getName().toStdString(),
        }};
    }

    tracktion::AutomatableParameter::Ptr selected_parameter;
    const int parameter_count = external_plugin->getNumAutomatableParameters();
    const juce::String target_parameter_id{parameter_id};
    for (int index = 0; index < parameter_count; ++index)
    {
        tracktion::AutomatableParameter::Ptr parameter =
            external_plugin->getAutomatableParameter(index);
        if (parameter != nullptr && parameter->paramID == target_parameter_id)
        {
            selected_parameter = std::move(parameter);
            break;
        }
    }

    if (selected_parameter == nullptr && parameter_index >= 0 && parameter_index < parameter_count)
    {
        tracktion::AutomatableParameter::Ptr indexed_parameter =
            external_plugin->getAutomatableParameter(parameter_index);
        if (indexed_parameter != nullptr &&
            (target_parameter_id.isEmpty() || indexed_parameter->paramID == target_parameter_id))
        {
            selected_parameter = std::move(indexed_parameter);
        }
    }

    if (selected_parameter == nullptr)
    {
        return std::unexpected{PluginHostError{
            PluginHostErrorCode::PluginStateRestoreFailed,
            "Plugin parameter was not found: " + parameter_id,
        }};
    }

    constexpr auto suppress_async_tail = std::chrono::milliseconds{250};
    m_impl->m_plugin_parameter_observation_suppressed_until =
        std::chrono::steady_clock::now() + suppress_async_tail;
    const juce::ScopedValueSetter<bool> suppress_parameter_observation(
        m_impl->m_plugin_parameter_observation_suppressed, true);
    selected_parameter->setNormalisedParameter(
        static_cast<float>(normalized_value), juce::sendNotification);
    return {};
}

// Settles or conservatively drops any pending plugin-parameter value edits.
void Engine::flushPendingPluginParameterEdits()
{
    if (!juce::MessageManager::getInstance()->isThisTheMessageThread())
    {
        return;
    }

    m_impl->flushPendingPluginParameterEdits();
}

// Reports whether any plugin-parameter value edit is waiting for gesture end or debounce.
bool Engine::hasPendingPluginParameterEdits() const
{
    if (!juce::MessageManager::getInstance()->isThisTheMessageThread())
    {
        return false;
    }

    return m_impl->hasPendingPluginParameterEdits();
}

// Stores the observer endpoint driven by Tracktion parameter callbacks.
void Engine::setPluginParameterEditObserver(PluginParameterEditObserver observer)
{
    if (!juce::MessageManager::getInstance()->isThisTheMessageThread())
    {
        return;
    }

    m_impl->m_plugin_parameter_edit_observer = std::move(observer);
    m_impl->m_plugin_parameter_pending_notified = false;
    m_impl->notifyPluginParameterPendingStateChanged();
}

// Stores the observer endpoint driven by Tracktion processor-wide plugin changes.
void Engine::setPluginStateEditObserver(PluginStateEditObserver observer)
{
    if (!juce::MessageManager::getInstance()->isThisTheMessageThread())
    {
        return;
    }

    m_impl->m_plugin_state_edit_observer = std::move(observer);
}

// Stores the app-level endpoint for hosted plugin-window Undo/Redo shortcuts.
void Engine::setPluginWindowCommandObserver(PluginWindowCommandObserver observer)
{
    if (!juce::MessageManager::getInstance()->isThisTheMessageThread())
    {
        return;
    }

    m_impl->m_plugin_window_command_observer = std::move(observer);
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
std::expected<void, LiveRigError> Engine::clearLiveRig()
{
    if (!juce::MessageManager::getInstance()->isThisTheMessageThread())
    {
        return std::unexpected{LiveRigError{LiveRigErrorCode::MessageThreadRequired}};
    }

    // Clear also cancels cooperative restore steps queued by loadLiveRig(); otherwise stale
    // continuations could rebuild the chain after the editor has closed the project.
    m_impl->m_load_op.reset();
    m_impl->m_plugin_parameter_observation_suppressed = false;

    if (m_impl->instrumentTrack() == nullptr)
    {
        return std::unexpected{LiveRigError{LiveRigErrorCode::TrackMissing}};
    }

    m_impl->stopTransportAndReleaseContext();
    auto cleared = m_impl->clearUserLiveRigPlugins();
    if (!cleared.has_value())
    {
        m_impl->refreshPluginParameterObservers();
        return std::unexpected{std::move(cleared.error())};
    }

    auto reset = m_impl->resetLiveRigProjectState();
    if (!reset.has_value())
    {
        m_impl->refreshPluginParameterObservers();
        return std::unexpected{std::move(reset.error())};
    }

    auto route_result = m_impl->rebuildInstrumentMonitoringGraph();
    if (!route_result.has_value())
    {
        m_impl->refreshPluginParameterObservers();
        return std::unexpected{liveRigErrorFromLiveInputError(route_result.error())};
    }
    m_impl->refreshPluginParameterObservers();
    return {};
}

// Reads the current input gain from the structural live-rig gain plugin.
Gain Engine::inputGain() const
{
    return m_impl->readGainFromPlugin(m_impl->m_input_gain_plugin_id);
}

// Reads the current output gain from the structural live-rig gain plugin.
Gain Engine::outputGain() const
{
    return m_impl->readGainFromPlugin(m_impl->m_output_gain_plugin_id);
}

// Sets the input gain on the structural live-rig gain plugin before the signal chain.
std::expected<void, LiveInputError> Engine::setInputGain(Gain gain)
{
    if (!juce::MessageManager::getInstance()->isThisTheMessageThread())
    {
        return std::unexpected{LiveInputError{LiveInputErrorCode::MessageThreadRequired}};
    }

    gain = clampGain(gain);
    auto applied = m_impl->applyGainToPlugin(m_impl->m_input_gain_plugin_id, gain);
    if (!applied.has_value())
    {
        return std::unexpected{liveInputErrorFromLiveRigError(applied.error())};
    }

    return {};
}

// Reads the live input meter used by the calibration window.
AudioMeterLevel Engine::rawInputMeterLevel() const
{
    return m_impl->rawInputMeterLevel();
}

// Reports whether calibrated live input is currently routed through the chain.
bool Engine::liveInputMonitoringEnabled() const
{
    return m_impl->m_live_input_monitoring_enabled;
}

// Enables or disables processed live input monitoring without changing transport playback.
std::expected<void, LiveInputError> Engine::setLiveInputMonitoringEnabled(bool enabled)
{
    if (!juce::MessageManager::getInstance()->isThisTheMessageThread())
    {
        return std::unexpected{LiveInputError{LiveInputErrorCode::MessageThreadRequired}};
    }

    return m_impl->setMonitoringChannelEnabled(
        MonitorChannel::LiveInput,
        enabled,
        currentInputDeviceIdentity().has_value(),
        "live input enable rollback failed");
}

// Reports whether unprocessed calibration input is currently routed directly to output.
bool Engine::calibrationInputMonitoringEnabled() const
{
    return m_impl->m_calibration_input_monitoring_enabled;
}

// Enables or disables direct calibration monitoring without changing transport playback.
std::expected<void, LiveInputError> Engine::setCalibrationInputMonitoringEnabled(bool enabled)
{
    if (!juce::MessageManager::getInstance()->isThisTheMessageThread())
    {
        return std::unexpected{LiveInputError{LiveInputErrorCode::MessageThreadRequired}};
    }

    return m_impl->setMonitoringChannelEnabled(
        MonitorChannel::Calibration,
        enabled,
        currentInputDeviceIdentity().has_value(),
        "calibration monitoring enable rollback failed");
}

// Sets the output gain on the structural live-rig gain plugin after the signal chain.
std::expected<void, LiveRigError> Engine::setOutputGain(Gain gain)
{
    if (!juce::MessageManager::getInstance()->isThisTheMessageThread())
    {
        return std::unexpected{LiveRigError{LiveRigErrorCode::MessageThreadRequired}};
    }

    gain = clampGain(gain);
    auto applied = m_impl->applyGainToPlugin(m_impl->m_output_gain_plugin_id, gain);
    if (!applied.has_value())
    {
        return std::unexpected{std::move(applied.error())};
    }

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

    const std::size_t user_plugin_count = m_impl->userVisiblePluginCount();
    if (user_plugin_count > max_signal_chain_plugins)
    {
        return std::unexpected{LiveRigError{
            LiveRigErrorCode::PluginChainLimitExceeded,
            pluginChainLimitExceededMessage(user_plugin_count),
        }};
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

        // Structural plugins are captured as authored tone state or runtime-only infrastructure,
        // not as chain entries.
        if (m_impl->isStructuralLiveRigPlugin(plugin))
        {
            continue;
        }

        auto* const external_plugin = dynamic_cast<tracktion::ExternalPlugin*>(plugin);
        if (external_plugin == nullptr)
        {
            m_impl->rebuildInstrumentMonitoringGraphBestEffort(
                "unsupported plugin capture rollback failed");
            return std::unexpected{LiveRigError{
                LiveRigErrorCode::UnsupportedPlugin,
                "Only external plugins can be captured right now: " +
                    plugin->getName().toStdString()
            }};
        }

        const std::size_t chain_index = captured_plugin_index;
        // The editor owns the visual placement; fall back to a gapless block when none is supplied.
        const std::size_t block_index = chain_index < request.block_indices.size()
                                            ? request.block_indices[chain_index]
                                            : chain_index;
        const std::string display_type_override =
            chain_index < request.display_type_overrides.size()
                ? request.display_type_overrides[chain_index]
                : std::string{};
        external_plugin->flushPluginStateToValueTree();
        juce::ValueTree plugin_state = external_plugin->state.createCopy();
        plugin_state.removeProperty(tracktion::IDs::id, nullptr);

        const std::filesystem::path plugin_state_ref =
            generatedPluginStatePath(plugin_state_directory, chain_index);
        const std::filesystem::path plugin_state_path = request.song_directory / plugin_state_ref;
        const auto plugin_state_xml = makePluginStateXml(plugin_state, plugin_state_path);
        if (!plugin_state_xml.has_value())
        {
            m_impl->rebuildInstrumentMonitoringGraphBestEffort(
                "plugin-state serialization rollback failed");
            return std::unexpected{plugin_state_xml.error()};
        }

        if (auto write_result = writeTextFile(
                plugin_state_path, *plugin_state_xml, LiveRigErrorCode::CouldNotWritePluginState);
            !write_result.has_value())
        {
            m_impl->rebuildInstrumentMonitoringGraphBestEffort(
                "plugin-state write rollback failed");
            return std::unexpected{std::move(write_result.error())};
        }

        document.chain.push_back(
            PluginRecord{
                .id = "plugin-" + std::to_string(chain_index + 1),
                .identity = makePluginIdentity(external_plugin->desc),
                .tracktion_state_ref = plugin_state_ref.generic_string(),
                .block_index = block_index,
                .display_type_override = display_type_override,
            });
        snapshot.plugins.push_back(makePluginChainEntry(*external_plugin, chain_index));
        snapshot.plugins.back().block_index = block_index;
        snapshot.plugins.back().display_type_override = display_type_override;
        ++captured_plugin_index;
    }

    // Read authored output gain from the structural plugin for persistence and snapshot.
    const Gain captured_output_gain = m_impl->readGainFromPlugin(m_impl->m_output_gain_plugin_id);
    document.output_gain = captured_output_gain;
    snapshot.output_gain = captured_output_gain;

    const std::filesystem::path tone_document_path = request.song_directory / tone_document_ref;
    if (auto write_result = writeToneDocument(tone_document_path, document);
        !write_result.has_value())
    {
        m_impl->rebuildInstrumentMonitoringGraphBestEffort("tone document write rollback failed");
        return std::unexpected{std::move(write_result.error())};
    }

    auto route_result = m_impl->rebuildInstrumentMonitoringGraph();
    if (!route_result.has_value())
    {
        return std::unexpected{liveRigErrorFromLiveInputError(route_result.error())};
    }
    return snapshot;
}

// Kicks off the cooperative async live rig load: validates the request, reads the tone document
// up front, clears the existing chain, and posts the first plugin step on the message loop so
// the busy overlay has a chance to paint before plugin construction starts.
void Engine::loadLiveRig(LiveRigLoadRequest request, LiveRigLoadResultCallback on_result)
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
        auto clear_result = clearLiveRig();
        if (!clear_result.has_value())
        {
            on_result(std::unexpected{std::move(clear_result.error())});
            return;
        }

        const Gain default_gain{defaultGainDb()};
        auto output_result = setOutputGain(default_gain);
        if (!output_result.has_value())
        {
            on_result(std::unexpected{std::move(output_result.error())});
            return;
        }

        on_result(
            LiveRigLoadResult{
                .plugins = {},
                .output_gain = default_gain,
            });
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

    if (m_impl->instrumentTrack() == nullptr)
    {
        on_result(std::unexpected{LiveRigError{LiveRigErrorCode::TrackMissing}});
        return;
    }

    m_impl->m_plugin_parameter_observation_suppressed = true;
    m_impl->stopTransportAndReleaseContext();
    auto cleared = m_impl->clearUserLiveRigPlugins();
    if (!cleared.has_value())
    {
        m_impl->m_plugin_parameter_observation_suppressed = false;
        m_impl->refreshPluginParameterObservers();
        on_result(std::unexpected{std::move(cleared.error())});
        return;
    }

    auto reset = m_impl->resetLiveRigProjectState();
    if (!reset.has_value())
    {
        m_impl->m_plugin_parameter_observation_suppressed = false;
        m_impl->refreshPluginParameterObservers();
        on_result(std::unexpected{std::move(reset.error())});
        return;
    }

    auto operation = std::make_unique<LiveRigLoadOperation>();
    operation->request = std::move(request);
    operation->chain = std::move(document->chain);
    operation->output_gain = document->output_gain;
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
        auto structural_valid = validateStructuralLiveRigPlugins();
        if (!structural_valid.has_value())
        {
            abortLiveRigLoad(std::move(structural_valid.error()));
            return;
        }

        auto output_gain_applied =
            applyGainToPlugin(m_output_gain_plugin_id, m_load_op->output_gain);
        if (!output_gain_applied.has_value())
        {
            abortLiveRigLoad(std::move(output_gain_applied.error()));
            return;
        }

        auto route_result = rebuildInstrumentMonitoringGraph();
        if (!route_result.has_value())
        {
            abortLiveRigLoad(liveRigErrorFromLiveInputError(route_result.error()));
            return;
        }

        m_plugin_parameter_observation_suppressed = false;
        refreshPluginParameterObservers();
        auto operation = std::move(m_load_op);
        operation->result.output_gain = operation->output_gain;
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

    const tracktion::Plugin* const output_gain = findStructuralGainPlugin(m_output_gain_plugin_id);
    const int insert_index =
        output_gain != nullptr ? instrument_track->pluginList.indexOf(output_gain) : -1;
    if (insert_index < 0)
    {
        abortLiveRigLoad(
            LiveRigError{
                LiveRigErrorCode::PluginRestoreFailed,
                "Structural live rig output gain plugin is missing",
            });
        return;
    }

    const tracktion::Plugin::Ptr inserted_plugin =
        instrument_track->pluginList.insertPlugin(*plugin_state, insert_index);
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
        makePluginChainEntry(*external_plugin, m_load_op->result.plugins.size()));
    // The runtime chain has no gap concept, so carry the authored block placement from the parsed
    // tone document into the restored chain entry.
    m_load_op->result.plugins.back().block_index = m_load_op->chain[plugin_index].block_index;
    m_load_op->result.plugins.back().display_type_override =
        m_load_op->chain[plugin_index].display_type_override;
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
    if (instrumentTrack() != nullptr)
    {
        auto cleared = clearUserLiveRigPlugins();
        if (!cleared.has_value())
        {
            logInstrumentMonitoringFailure(toJuceString(cleared.error().message));
        }
        auto reset = resetLiveRigProjectState();
        if (!reset.has_value())
        {
            logInstrumentMonitoringFailure(toJuceString(reset.error().message));
        }
    }
    rebuildInstrumentMonitoringGraphBestEffort("live rig load abort rollback failed");
    m_plugin_parameter_observation_suppressed = false;
    refreshPluginParameterObservers();
    operation->on_result(std::unexpected{std::move(error)});
}

// Exposes the JUCE device manager so settings UI can present and apply hardware route choices.
juce::AudioDeviceManager& Engine::deviceManager() noexcept
{
    return m_impl->m_engine->getDeviceManager().deviceManager;
}

// Restores the JUCE device manager state captured by a previous editor session.
std::expected<void, AudioDeviceConfigurationError> Engine::restoreSerializedDeviceState(
    const std::string& serialized_state)
{
    if (!juce::MessageManager::getInstance()->isThisTheMessageThread())
    {
        return std::unexpected{
            AudioDeviceConfigurationError{AudioDeviceConfigurationErrorCode::MessageThreadRequired}
        };
    }

    const std::unique_ptr<juce::XmlElement> xml =
        juce::parseXML(juce::String{serialized_state.c_str()});
    if (xml == nullptr)
    {
        return std::unexpected{
            AudioDeviceConfigurationError{AudioDeviceConfigurationErrorCode::InvalidSerializedState}
        };
    }

    const juce::String error_text =
        m_impl->m_engine->getDeviceManager().deviceManager.initialise(1, 2, xml.get(), true);
    if (error_text.isNotEmpty())
    {
        return std::unexpected{AudioDeviceConfigurationError{
            AudioDeviceConfigurationErrorCode::RestoreFailed, error_text.toStdString()
        }};
    }

    auto route_result = m_impl->rebuildInstrumentMonitoringGraph();
    if (!route_result.has_value())
    {
        return std::unexpected{AudioDeviceConfigurationError{
            AudioDeviceConfigurationErrorCode::RestoreFailed, route_result.error().message
        }};
    }

    return {};
}

// Captures the JUCE device manager state as an opaque string for editor settings.
std::optional<std::string> Engine::serializedDeviceState() const
{
    const std::unique_ptr<juce::XmlElement> xml =
        m_impl->m_engine->getDeviceManager().deviceManager.createStateXml();
    if (xml == nullptr)
    {
        return std::nullopt;
    }

    return xml->toString().toStdString();
}

// Captures open-device timing and route details through the JUCE device manager.
AudioDeviceStatus Engine::currentDeviceStatus() const
{
    auto* const current_device =
        m_impl->m_engine->getDeviceManager().deviceManager.getCurrentAudioDevice();
    if (current_device == nullptr || !current_device->isOpen())
    {
        return {};
    }

    const double sample_rate_hz = current_device->getCurrentSampleRate();
    if (sample_rate_hz <= 0.0)
    {
        return {};
    }

    return AudioDeviceStatus{
        .open = true,
        .device_name = current_device->getName().toStdString(),
        .backend_name = current_device->getTypeName().toStdString(),
        .sample_rate_hz = sample_rate_hz,
        .bit_depth = current_device->getCurrentBitDepth(),
        .input_channels = current_device->getActiveInputChannels().countNumberOfSetBits(),
        .output_channels = current_device->getActiveOutputChannels().countNumberOfSetBits(),
        .buffer_size_samples = current_device->getCurrentBufferSizeSamples(),
        .input_latency_ms =
            samplesToMilliseconds(current_device->getInputLatencyInSamples(), sample_rate_hz),
        .output_latency_ms =
            samplesToMilliseconds(current_device->getOutputLatencyInSamples(), sample_rate_hz),
    };
}

// Captures the one-channel physical input route used to validate calibration state.
std::optional<InputDeviceIdentity> Engine::currentInputDeviceIdentity() const
{
    if (m_impl->m_audio_device_configuration_refresh_pending)
    {
        return std::nullopt;
    }

    const auto& device_manager = m_impl->m_engine->getDeviceManager().deviceManager;
    auto* const current_device = device_manager.getCurrentAudioDevice();
    if (current_device == nullptr || !current_device->isOpen())
    {
        return std::nullopt;
    }

    juce::AudioDeviceManager::AudioDeviceSetup setup;
    device_manager.getAudioDeviceSetup(setup);
    if (setup.inputDeviceName.isEmpty())
    {
        return std::nullopt;
    }

    const juce::BigInteger active_inputs = current_device->getActiveInputChannels();
    const int first_channel = active_inputs.findNextSetBit(0);
    if (first_channel < 0 || active_inputs.findNextSetBit(first_channel + 1) >= 0)
    {
        return std::nullopt;
    }

    const juce::StringArray input_channel_names = current_device->getInputChannelNames();
    juce::String input_channel_name;
    if (first_channel < input_channel_names.size())
    {
        input_channel_name = input_channel_names[first_channel];
    }

    InputDeviceIdentity identity{
        .backend_name = device_manager.getCurrentAudioDeviceType().toStdString(),
        .input_device_name = setup.inputDeviceName.toStdString(),
        .input_channel_index = first_channel,
        .input_channel_name = input_channel_name.toStdString(),
    };
    if (!isValidInputDeviceIdentity(identity))
    {
        return std::nullopt;
    }

    return identity;
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

// Reads Tracktion meter clients through the pimpl without exposing Tracktion meter types.
AudioMeterSnapshot Engine::audioMeterSnapshot() const
{
    return m_impl->audioMeterSnapshot();
}

// Creates an IThumbnail wrapper without exposing Tracktion types through public UI-facing headers.
std::unique_ptr<IThumbnail> Engine::createThumbnail(juce::Component& owner)
{
    return std::make_unique<TracktionThumbnail>(*m_impl->m_engine, owner);
}

} // namespace rock_hero::common::audio
