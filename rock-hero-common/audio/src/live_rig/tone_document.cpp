#include "live_rig/tone_document.h"

#include "shared/audio_path_util.h"

#include <fstream>
#include <rock_hero/common/core/package/package_id.h>
#include <rock_hero/common/core/package/workspace_paths.h>
#include <rock_hero/common/core/shared/application_identity.h>
#include <rock_hero/common/core/shared/json.h>
#include <rock_hero/common/core/shared/juce_path.h>
#include <rock_hero/common/core/shared/logger.h>
#include <system_error>
#include <utility>

namespace rock_hero::common::audio
{

namespace
{

constexpr std::string_view g_tone_state_directory_name{"state"};
constexpr std::string_view g_plugin_state_extension{".tracktion-plugin"};

// Every tone-document property name this file both writes and reads back. They are named here
// because the writer and the reader are one round trip: a key spelled twice can drift, and the
// reader treats an unknown name as absence rather than as an error, so a drifted key silently loses
// the field instead of failing the load. Write-only placeholders for the not-yet-parsed parts of
// the format stay inline below, because nothing reads them to disagree with.
constexpr const char* g_format_version_key{"formatVersion"};
constexpr const char* g_slots_key{"slots"};
constexpr const char* g_slot_chain_key{"chain"};
constexpr const char* g_slot_output_gain_db_key{"outputGainDb"};

constexpr const char* g_plugin_id_key{"id"};
constexpr const char* g_plugin_identity_key{"identity"};
constexpr const char* g_plugin_tracktion_state_key{"tracktionState"};
constexpr const char* g_plugin_block_index_key{"blockIndex"};
constexpr const char* g_plugin_display_type_override_key{"displayTypeOverride"};
constexpr const char* g_plugin_stable_id_key{"stableId"};

constexpr const char* g_identity_format_key{"format"};
constexpr const char* g_identity_name_key{"name"};
constexpr const char* g_identity_descriptive_name_key{"descriptiveName"};
constexpr const char* g_identity_manufacturer_key{"manufacturer"};
constexpr const char* g_identity_version_key{"version"};
constexpr const char* g_identity_unique_id_key{"uniqueId"};
constexpr const char* g_identity_deprecated_uid_key{"deprecatedUid"};
constexpr const char* g_identity_is_instrument_key{"isInstrument"};
constexpr const char* g_identity_original_file_key{"originalFileOrIdentifier"};
constexpr const char* g_identity_juce_hint_key{"juceIdentifierHint"};
constexpr const char* g_identity_tracktion_hint_key{"tracktionIdentifierHint"};

// Serializes identity to the tone document's JSON shape. The tone document owns this shape, so its
// writer and its reader below live together rather than one per feature folder.
[[nodiscard]] juce::var makeIdentityJson(const PluginIdentity& identity)
{
    return core::Json::makeObject({
        {g_identity_format_key, core::Json::makeString(identity.format_name)},
        {g_identity_name_key, core::Json::makeString(identity.name)},
        {g_identity_descriptive_name_key, core::Json::makeString(identity.descriptive_name)},
        {g_identity_manufacturer_key, core::Json::makeString(identity.manufacturer)},
        {g_identity_version_key, core::Json::makeString(identity.version)},
        {g_identity_unique_id_key, core::Json::makeString(identity.unique_id)},
        {g_identity_deprecated_uid_key, core::Json::makeString(identity.deprecated_uid)},
        {g_identity_is_instrument_key, juce::var{identity.is_instrument}},
        {g_identity_original_file_key,
         core::Json::makeString(identity.original_file_or_identifier)},
        {g_identity_juce_hint_key, core::Json::makeString(identity.juce_identifier_hint)},
        {g_identity_tracktion_hint_key, core::Json::makeString(identity.tracktion_identifier_hint)},
    });
}

// Reads the identity object for one tone plugin record.
[[nodiscard]] PluginIdentity readIdentityJson(const juce::var& object)
{
    return PluginIdentity{
        .format_name = core::Json::readOptionalString(object, g_identity_format_key),
        .name = core::Json::readOptionalString(object, g_identity_name_key),
        .descriptive_name = core::Json::readOptionalString(object, g_identity_descriptive_name_key),
        .manufacturer = core::Json::readOptionalString(object, g_identity_manufacturer_key),
        .version = core::Json::readOptionalString(object, g_identity_version_key),
        .unique_id = core::Json::readOptionalString(object, g_identity_unique_id_key),
        .deprecated_uid = core::Json::readOptionalString(object, g_identity_deprecated_uid_key),
        .is_instrument = core::Json::readOptionalBool(object, g_identity_is_instrument_key),
        .original_file_or_identifier =
            core::Json::readOptionalString(object, g_identity_original_file_key),
        .juce_identifier_hint = core::Json::readOptionalString(object, g_identity_juce_hint_key),
        .tracktion_identifier_hint =
            core::Json::readOptionalString(object, g_identity_tracktion_hint_key),
    };
}

} // namespace

// Resolves a package-relative file and verifies it exists inside the song workspace.
[[nodiscard]] std::optional<std::filesystem::path> resolvePackageFile(
    const std::filesystem::path& song_directory, const std::string& relative_path)
{
    const std::filesystem::path package_path{relative_path};
    if (!core::isSafeRelativePath(package_path))
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
    return core::isSafeRelativePath(plugin_state_path) &&
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

// Serializes the v1 tone document subset used by the current linear chain.
[[nodiscard]] juce::var makeToneDocumentJson(const ToneDocument& document)
{
    juce::var chain = core::Json::makeArray();
    for (const PluginRecord& plugin : document.chain)
    {
        const juce::var plugin_json = core::Json::makeObject({
            {g_plugin_id_key, core::Json::makeString(plugin.id)},
            {g_plugin_identity_key, makeIdentityJson(plugin.identity)},
            {g_plugin_tracktion_state_key, core::Json::makeString(plugin.tracktion_state_ref)},
            {g_plugin_block_index_key, juce::var{static_cast<int>(plugin.block_index)}},
        });
        if (!plugin.display_type_override.empty())
        {
            plugin_json.getDynamicObject()->setProperty(
                g_plugin_display_type_override_key,
                core::Json::makeString(plugin.display_type_override));
        }
        if (!plugin.stable_id.empty())
        {
            plugin_json.getDynamicObject()->setProperty(
                g_plugin_stable_id_key, core::Json::makeString(plugin.stable_id));
        }

        chain.append(plugin_json);
    }

    juce::var slots = core::Json::makeArray();
    slots.append(
        core::Json::makeObject({
            {"id", core::Json::makeString("default")},
            {"name", core::Json::makeString("Default")},
            {g_slot_chain_key, chain},
            {"automation", core::Json::makeArray()},
            {g_slot_output_gain_db_key, juce::var{document.output_gain.db}},
        }));

    juce::var tone_clips = core::Json::makeArray();
    tone_clips.append(
        core::Json::makeObject({
            {"slot", core::Json::makeString("default")},
            {"startSeconds", juce::var{0.0}},
            {"endSeconds", juce::var{}},
        }));

    return core::Json::makeObject({
        {g_format_version_key, juce::var{1}},
        {g_slots_key, slots},
        {"toneClips", tone_clips},
    });
}

// Parses the v1 tone document JSON, validating structure and sidecar-reference shape only.
[[nodiscard]] std::expected<ToneDocument, LiveRigError> parseToneDocumentJson(
    const juce::var& document_json, const std::filesystem::path& expected_state_directory)
{
    if (!document_json.isObject() ||
        core::Json::readOptionalInt(document_json, g_format_version_key, 0) != 1)
    {
        return std::unexpected{LiveRigError{
            LiveRigErrorCode::InvalidToneDocument, "Unsupported tone document formatVersion"
        }};
    }

    const juce::var& slots_json = core::Json::value(document_json, g_slots_key);
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

    const juce::var& chain_json = core::Json::value(default_slot_json, g_slot_chain_key);
    if (!chain_json.isArray())
    {
        return std::unexpected{LiveRigError{
            LiveRigErrorCode::InvalidToneDocument, "Tone document slot chain must be an array"
        }};
    }

    const auto plugin_count = static_cast<std::size_t>(chain_json.size());
    if (plugin_count > g_max_signal_chain_plugins)
    {
        return std::unexpected{LiveRigError{
            LiveRigErrorCode::PluginChainLimitExceeded,
            pluginChainLimitExceededMessage(plugin_count),
        }};
    }

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

        const auto id = core::Json::tryReadString(plugin_json, g_plugin_id_key);
        const auto tracktion_state =
            core::Json::tryReadString(plugin_json, g_plugin_tracktion_state_key);
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

        const juce::var& identity_json = core::Json::value(plugin_json, g_plugin_identity_key);
        // Block placement is editor-owned metadata; carry the raw value through opaquely and leave
        // interpretation (validity, gap rules, fallback) to the editor. Absent or negative values
        // default to zero and are resolved by the editor like any other invalid placement.
        const std::optional<std::int64_t> block_index_value =
            core::Json::tryReadInt64(plugin_json, g_plugin_block_index_key);
        const std::size_t block_index = block_index_value.has_value() && *block_index_value >= 0
                                            ? static_cast<std::size_t>(*block_index_value)
                                            : 0;
        document.chain.push_back(
            PluginRecord{
                .id = *id,
                .identity =
                    identity_json.isObject() ? readIdentityJson(identity_json) : PluginIdentity{},
                .tracktion_state_ref = *tracktion_state,
                .block_index = block_index,
                .display_type_override =
                    core::Json::readOptionalString(plugin_json, g_plugin_display_type_override_key),
                .stable_id = core::Json::readOptionalString(plugin_json, g_plugin_stable_id_key),
            });
    }

    // The output gain is optional and defaults to 0.0 dB when absent.
    document.output_gain = clampGain(
        Gain{core::Json::readOptionalDouble(
            default_slot_json, g_slot_output_gain_db_key, defaultGainDb())});

    return document;
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

    auto document = parseToneDocumentJson(
        *parsed_document, toneDocumentStateDirectory(std::filesystem::path{tone_document_ref}));
    if (!document.has_value())
    {
        return std::unexpected{std::move(document.error())};
    }

    // Sidecar existence is a workspace concern, so it stays out of the shared JSON parse: the
    // tone-file reader checks the same references against its archive entry list instead.
    for (const PluginRecord& plugin : document->chain)
    {
        if (!resolvePackageFile(song_directory, plugin.tracktion_state_ref).has_value())
        {
            return std::unexpected{LiveRigError{
                LiveRigErrorCode::MissingPluginState,
                "Tone plugin state is missing or unsafe: " + plugin.tracktion_state_ref
            }};
        }
    }

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

namespace
{

// Shared validation tail for plugin-state XML parsed from a sidecar file or archive entry text.
// Strips the live item id so a restored tree can never collide with an existing instance.
[[nodiscard]] std::expected<juce::ValueTree, LiveRigError> pluginStateTreeFromParsedXml(
    const juce::XmlElement* xml, const std::string& state_ref)
{
    if (xml == nullptr)
    {
        return std::unexpected{LiveRigError{
            LiveRigErrorCode::CouldNotReadPluginState,
            "Could not parse tone plugin state: " + state_ref
        }};
    }

    juce::ValueTree tree = juce::ValueTree::fromXml(*xml);
    if (!tree.isValid())
    {
        return std::unexpected{LiveRigError{
            LiveRigErrorCode::CouldNotReadPluginState,
            "Tone plugin state is not a valid ValueTree: " + state_ref
        }};
    }

    tree.removeProperty(tracktion::IDs::id, nullptr);
    return tree;
}

} // namespace

// Reads a Tracktion plugin-state sidecar into a ValueTree.
[[nodiscard]] std::expected<juce::ValueTree, LiveRigError> readPluginStateTree(
    const std::filesystem::path& plugin_state_path)
{
    const std::unique_ptr<juce::XmlElement> xml =
        juce::parseXML(common::core::juceFileFromPath(plugin_state_path));
    return pluginStateTreeFromParsedXml(xml.get(), plugin_state_path.string());
}

// Parses Tracktion plugin-state XML text into a ValueTree.
[[nodiscard]] std::expected<juce::ValueTree, LiveRigError> pluginStateTreeFromXmlText(
    const std::string& xml_text, const std::string& state_ref)
{
    const std::unique_ptr<juce::XmlElement> xml =
        juce::parseXML(juce::String::fromUTF8(xml_text.c_str()));
    return pluginStateTreeFromParsedXml(xml.get(), state_ref);
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
            .display_type_override = {},
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
        .display_type_override = {},
    };
}

} // namespace rock_hero::common::audio
