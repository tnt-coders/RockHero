#pragma once

#include <cstdint>
#include <expected>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace rock_hero::common::audio
{

enum class PluginCatalogMetadataSource
{
    Filename,
    ModuleInfo,
    VersionResource,
    PluginFactory,
    Path,
    Validated,
};

struct PluginCatalogFileStamp
{
    std::uintmax_t size_bytes{};
    std::int64_t last_write_time{};

    friend bool operator==(const PluginCatalogFileStamp& lhs, const PluginCatalogFileStamp& rhs) =
        default;
};

struct PluginCatalogRecord
{
    std::string path_key;
    std::filesystem::path file_path;
    std::string format_name{"VST3"};
    std::string name;
    std::string manufacturer;
    PluginCatalogMetadataSource metadata_source{PluginCatalogMetadataSource::Filename};
    std::optional<PluginCatalogFileStamp> module_file_stamp;
    std::optional<PluginCatalogFileStamp> moduleinfo_file_stamp;
};

struct PluginCatalogDocument
{
    std::vector<PluginCatalogRecord> records;
};

struct PluginCatalogStaticMetadata
{
    std::string name;
    std::string manufacturer;
    PluginCatalogMetadataSource source{PluginCatalogMetadataSource::Filename};
    std::optional<PluginCatalogFileStamp> moduleinfo_file_stamp;
};

struct PluginCatalogCacheError
{
    enum class Code
    {
        InvalidJson,
        UnsupportedSchema,
        CouldNotRead,
        CouldNotWrite,
    };

    Code code{};
    std::string message;
};

[[nodiscard]] std::string pluginCatalogPathToUtf8String(const std::filesystem::path& path);

[[nodiscard]] std::expected<PluginCatalogDocument, PluginCatalogCacheError> loadPluginCatalogCache(
    const std::filesystem::path& cache_file);

[[nodiscard]] std::expected<void, PluginCatalogCacheError> savePluginCatalogCache(
    const std::filesystem::path& cache_file, const PluginCatalogDocument& document);

[[nodiscard]] std::optional<PluginCatalogFileStamp> pluginCatalogFileStamp(
    const std::filesystem::path& path);

[[nodiscard]] bool isPluginCatalogRecordCurrent(
    const PluginCatalogRecord& record,
    const std::optional<PluginCatalogFileStamp>& module_file_stamp,
    const std::optional<PluginCatalogFileStamp>& moduleinfo_file_stamp);

[[nodiscard]] std::optional<std::filesystem::path> findVst3ModuleInfoPath(
    const std::filesystem::path& plugin_path);

[[nodiscard]] std::optional<PluginCatalogStaticMetadata> readVst3StaticMetadata(
    const std::filesystem::path& plugin_path);

} // namespace rock_hero::common::audio
