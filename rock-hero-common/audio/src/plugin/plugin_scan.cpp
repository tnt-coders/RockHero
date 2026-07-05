#include "plugin_scan.h"

#include "shared/audio_path_util.h"

#include <rock_hero/common/core/shared/json.h>
#include <rock_hero/common/core/shared/logger.h>
#include <tracktion_engine/tracktion_engine.h>
#include <utility>

namespace rock_hero::common::audio
{

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
            "Plugin validation failed plugin_path={:?} duration_ms={} error={:?}",
            plugin_path_text,
            total_duration.count(),
            *failure_message);
    }
    else
    {
        RH_LOG_INFO(
            "audio.plugin_validation",
            "Plugin validation succeeded plugin_path={:?} duration_ms={}",
            plugin_path_text,
            total_duration.count());
    }
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

} // namespace rock_hero::common::audio
