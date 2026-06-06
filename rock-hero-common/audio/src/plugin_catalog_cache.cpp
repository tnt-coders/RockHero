#include "plugin_catalog_cache.h"

#include <algorithm>
#include <bit>
#include <cctype>
#include <cstddef>
#include <cstdio>
#include <fstream>
#include <juce_audio_processors_headless/format_types/VST3_SDK/public.sdk/source/vst/moduleinfo/moduleinfoparser.h>
#include <juce_core/juce_core.h>
#include <limits>
#include <memory>
#include <pluginterfaces/base/ipluginbase.h>
#include <rock_hero/common/core/json.h>
#include <sstream>
#include <string_view>
#include <system_error>
#include <utility>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace rock_hero::common::audio
{

namespace
{

constexpr int g_plugin_catalog_cache_schema_version = 1;
constexpr std::string_view g_vst_audio_effect_class{"Audio Module Class"};

[[nodiscard]] PluginCatalogCacheError makeCacheError(
    PluginCatalogCacheError::Code code, std::string message)
{
    return PluginCatalogCacheError{.code = code, .message = std::move(message)};
}

[[nodiscard]] std::string metadataSourceToString(PluginCatalogMetadataSource source)
{
    switch (source)
    {
        case PluginCatalogMetadataSource::Filename:
        {
            return "filename";
        }
        case PluginCatalogMetadataSource::ModuleInfo:
        {
            return "moduleinfo";
        }
        case PluginCatalogMetadataSource::VersionResource:
        {
            return "version-resource";
        }
        case PluginCatalogMetadataSource::PluginFactory:
        {
            return "plugin-factory";
        }
        case PluginCatalogMetadataSource::Path:
        {
            return "path";
        }
        case PluginCatalogMetadataSource::Validated:
        {
            return "validated";
        }
    }

    return "filename";
}

[[nodiscard]] std::optional<PluginCatalogMetadataSource> metadataSourceFromString(
    const std::string& source)
{
    if (source == "filename")
    {
        return PluginCatalogMetadataSource::Filename;
    }
    if (source == "moduleinfo")
    {
        return PluginCatalogMetadataSource::ModuleInfo;
    }
    if (source == "version-resource")
    {
        return PluginCatalogMetadataSource::VersionResource;
    }
    if (source == "plugin-factory")
    {
        return PluginCatalogMetadataSource::PluginFactory;
    }
    if (source == "path")
    {
        return PluginCatalogMetadataSource::Path;
    }
    if (source == "validated")
    {
        return PluginCatalogMetadataSource::Validated;
    }

    return std::nullopt;
}

[[nodiscard]] juce::var makeFileStampJson(const PluginCatalogFileStamp& stamp)
{
    return common::core::Json::makeObject({
        {"size_bytes", static_cast<juce::int64>(stamp.size_bytes)},
        {"last_write_time", static_cast<juce::int64>(stamp.last_write_time)},
    });
}

[[nodiscard]] juce::var makeNullableFileStampJson(
    const std::optional<PluginCatalogFileStamp>& stamp)
{
    return stamp.has_value() ? makeFileStampJson(*stamp) : juce::var{};
}

[[nodiscard]] juce::var makePluginCatalogRecordJson(const PluginCatalogRecord& record)
{
    return common::core::Json::makeObject({
        {"path_key", common::core::Json::makeString(record.path_key)},
        {"file_path",
         common::core::Json::makeString(
             pluginCatalogPathToUtf8String(record.file_path.lexically_normal()))},
        {"format_name", common::core::Json::makeString(record.format_name)},
        {"name", common::core::Json::makeString(record.name)},
        {"manufacturer", common::core::Json::makeString(record.manufacturer)},
        {"metadata_source",
         common::core::Json::makeString(metadataSourceToString(record.metadata_source))},
        {"module_file_stamp", makeNullableFileStampJson(record.module_file_stamp)},
        {"moduleinfo_file_stamp", makeNullableFileStampJson(record.moduleinfo_file_stamp)},
    });
}

[[nodiscard]] juce::var makePluginCatalogDocumentJson(const PluginCatalogDocument& document)
{
    juce::Array<juce::var> record_values;
    record_values.ensureStorageAllocated(static_cast<int>(document.records.size()));
    for (const PluginCatalogRecord& record : document.records)
    {
        record_values.add(makePluginCatalogRecordJson(record));
    }

    return common::core::Json::makeObject({
        {"schema_version", g_plugin_catalog_cache_schema_version},
        {"records", juce::var{record_values}},
    });
}

[[nodiscard]] std::expected<std::optional<PluginCatalogFileStamp>, PluginCatalogCacheError>
readOptionalFileStamp(const juce::var& object, std::string_view property_name)
{
    const juce::var& stamp_value = common::core::Json::value(object, property_name);
    if (stamp_value.isVoid() || stamp_value.isUndefined())
    {
        return std::optional<PluginCatalogFileStamp>{};
    }

    if (!stamp_value.isObject())
    {
        return std::unexpected{makeCacheError(
            PluginCatalogCacheError::Code::InvalidJson,
            "Plugin catalog file stamp is not an object")};
    }

    const std::optional<std::int64_t> size =
        common::core::Json::tryReadInt64(stamp_value, "size_bytes");
    const std::optional<std::int64_t> last_write_time =
        common::core::Json::tryReadInt64(stamp_value, "last_write_time");
    if (!size.has_value() || *size < 0 || !last_write_time.has_value())
    {
        return std::unexpected{makeCacheError(
            PluginCatalogCacheError::Code::InvalidJson,
            "Plugin catalog file stamp is missing required fields")};
    }

    return PluginCatalogFileStamp{
        .size_bytes = static_cast<std::uintmax_t>(*size),
        .last_write_time = *last_write_time,
    };
}

[[nodiscard]] std::expected<PluginCatalogRecord, PluginCatalogCacheError> readPluginCatalogRecord(
    const juce::var& record_value)
{
    if (!record_value.isObject())
    {
        return std::unexpected{makeCacheError(
            PluginCatalogCacheError::Code::InvalidJson, "Plugin catalog record is not an object")};
    }

    const std::optional<std::string> path_key =
        common::core::Json::tryReadString(record_value, "path_key");
    const std::optional<std::string> file_path =
        common::core::Json::tryReadString(record_value, "file_path");
    const std::string metadata_source_text =
        common::core::Json::readOptionalString(record_value, "metadata_source", "filename");
    const std::optional<PluginCatalogMetadataSource> metadata_source =
        metadataSourceFromString(metadata_source_text);
    auto module_file_stamp = readOptionalFileStamp(record_value, "module_file_stamp");
    if (!module_file_stamp.has_value())
    {
        return std::unexpected{std::move(module_file_stamp.error())};
    }

    auto moduleinfo_file_stamp = readOptionalFileStamp(record_value, "moduleinfo_file_stamp");
    if (!moduleinfo_file_stamp.has_value())
    {
        return std::unexpected{std::move(moduleinfo_file_stamp.error())};
    }

    if (!path_key.has_value() || path_key->empty() || !file_path.has_value() ||
        file_path->empty() || !metadata_source.has_value() || !module_file_stamp->has_value())
    {
        return std::unexpected{makeCacheError(
            PluginCatalogCacheError::Code::InvalidJson,
            "Plugin catalog record is missing required fields")};
    }

    return PluginCatalogRecord{
        .path_key = *path_key,
        .file_path = std::filesystem::path{*file_path},
        .format_name = common::core::Json::readOptionalString(record_value, "format_name", "VST3"),
        .name = common::core::Json::readOptionalString(record_value, "name"),
        .manufacturer = common::core::Json::readOptionalString(record_value, "manufacturer"),
        .metadata_source = *metadata_source,
        .module_file_stamp = **module_file_stamp,
        .moduleinfo_file_stamp = *moduleinfo_file_stamp,
    };
}

[[nodiscard]] std::string readFileToString(const std::filesystem::path& path)
{
    std::ifstream file{path, std::ios::binary};
    std::ostringstream contents;
    contents << file.rdbuf();
    return contents.str();
}

[[nodiscard]] std::string lowerExtension(const std::filesystem::path& path)
{
    std::string extension = path.extension().string();
    std::ranges::transform(extension, extension.begin(), [](const unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return extension;
}

[[nodiscard]] bool isVst3PluginPath(const std::filesystem::path& path)
{
    return lowerExtension(path) == ".vst3";
}

[[nodiscard]] std::optional<std::filesystem::path> existingModuleInfoPathInBundle(
    const std::filesystem::path& bundle_path)
{
    const std::filesystem::path contents_path = bundle_path / "Contents";
    const std::filesystem::path resource_path = contents_path / "Resources" / "moduleinfo.json";
    const std::filesystem::path legacy_path = contents_path / "moduleinfo.json";

    std::error_code error;
    if (std::filesystem::is_regular_file(resource_path, error))
    {
        return resource_path;
    }
    error.clear();
    if (std::filesystem::is_regular_file(legacy_path, error))
    {
        return legacy_path;
    }

    return std::nullopt;
}

[[nodiscard]] std::optional<std::filesystem::path> vst3BundleRootForPath(
    const std::filesystem::path& plugin_path)
{
    if (!isVst3PluginPath(plugin_path))
    {
        return std::nullopt;
    }

    std::error_code error;
    if (std::filesystem::is_directory(plugin_path, error))
    {
        return plugin_path;
    }

    const std::filesystem::path architecture_path = plugin_path.parent_path();
    const std::filesystem::path contents_path = architecture_path.parent_path();
    const std::filesystem::path bundle_path = contents_path.parent_path();
    if (contents_path.filename() == "Contents" && isVst3PluginPath(bundle_path))
    {
        return bundle_path;
    }

    return std::nullopt;
}

[[nodiscard]] std::string trimString(const std::string& text)
{
    const auto first = std::ranges::find_if_not(
        text, [](const unsigned char character) { return std::isspace(character) != 0; });
    const auto last =
        std::ranges::find_if_not(text.rbegin(), text.rend(), [](const auto character) {
            return std::isspace(static_cast<unsigned char>(character)) != 0;
        }).base();

    if (first >= last)
    {
        return {};
    }

    return std::string{first, last};
}

[[nodiscard]] std::string lowerString(std::string text)
{
    std::ranges::transform(text, text.begin(), [](const unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return text;
}

[[nodiscard]] bool isUsefulMetadataText(const std::string& text)
{
    const std::string normalized = lowerString(trimString(text));
    return !normalized.empty() && normalized != "unknown" && normalized != "todo" &&
           normalized != "n/a" && normalized != "none" && normalized != "null" &&
           normalized != "__mycompanyname__" && normalized != "mycompany" &&
           normalized != "your company";
}

[[nodiscard]] std::string usefulMetadataText(const std::string& text)
{
    const std::string trimmed = trimString(text);
    return isUsefulMetadataText(trimmed) ? trimmed : std::string{};
}

[[nodiscard]] std::string moduleInfoManufacturer(
    const Steinberg::ModuleInfo& module_info,
    const std::vector<Steinberg::ModuleInfo::ClassInfo>& audio_classes)
{
    if (std::string manufacturer = usefulMetadataText(module_info.factoryInfo.vendor);
        !manufacturer.empty())
    {
        return manufacturer;
    }

    const auto find_class_vendor = [](const auto& classes) {
        for (const Steinberg::ModuleInfo::ClassInfo& class_info : classes)
        {
            if (std::string manufacturer = usefulMetadataText(class_info.vendor);
                !manufacturer.empty())
            {
                return manufacturer;
            }
        }

        return std::string{};
    };

    if (std::string manufacturer = find_class_vendor(audio_classes); !manufacturer.empty())
    {
        return manufacturer;
    }

    return find_class_vendor(module_info.classes);
}

#if defined(_WIN32)
struct VersionResourceTranslation
{
    WORD language{};
    WORD code_page{};

    friend bool operator==(
        const VersionResourceTranslation& lhs, const VersionResourceTranslation& rhs) = default;
};

// Unloads a VST3 module after optional module-level cleanup has run.
class WindowsVst3Module final
{
public:
    using ExitDllProc = bool(PLUGIN_API*)();

    // Takes ownership of a module loaded with LoadLibraryW().
    explicit WindowsVst3Module(HMODULE module) noexcept
        : m_module(module)
    {}

    // Calls the optional VST3 module exit hook before unloading the DLL.
    ~WindowsVst3Module()
    {
        if (m_module == nullptr)
        {
            return;
        }

        if (const auto exit_dll = function<ExitDllProc>("ExitDll"); exit_dll != nullptr)
        {
            exit_dll();
        }

        FreeLibrary(m_module);
    }

    WindowsVst3Module(const WindowsVst3Module&) = delete;
    WindowsVst3Module& operator=(const WindowsVst3Module&) = delete;
    WindowsVst3Module(WindowsVst3Module&&) = delete;
    WindowsVst3Module& operator=(WindowsVst3Module&&) = delete;

    // Reports whether the module loaded successfully.
    [[nodiscard]] explicit operator bool() const noexcept
    {
        return m_module != nullptr;
    }

    // Looks up a VST3 exported function by name.
    template <typename Function> [[nodiscard]] Function function(const char* name) const noexcept
    {
        return reinterpret_cast<Function>(GetProcAddress(m_module, name));
    }

private:
    // Loaded plugin module owned by this helper.
    HMODULE m_module{};
};

// Releases the factory reference returned by GetPluginFactory().
struct Vst3FactoryReleaser
{
    // Balances the factory reference acquired from the VST3 module.
    void operator()(Steinberg::IPluginFactory* factory) const noexcept
    {
        if (factory != nullptr)
        {
            factory->release();
        }
    }
};

[[nodiscard]] std::string fixedChar8MetadataText(const Steinberg::char8* text, std::size_t capacity)
{
    const auto* const first = reinterpret_cast<const char*>(text);
    const auto* const last = std::find(first, first + capacity, '\0');
    return usefulMetadataText(std::string{first, last});
}

[[nodiscard]] std::optional<std::string> versionResourceString(
    const std::vector<std::byte>& buffer, const VersionResourceTranslation translation,
    const wchar_t* key)
{
    wchar_t sub_block[96]{};
    if (swprintf_s(
            sub_block,
            L"\\StringFileInfo\\%04x%04x\\%s",
            translation.language,
            translation.code_page,
            key) <= 0)
    {
        return std::nullopt;
    }

    LPVOID value{};
    UINT value_length{};
    if (VerQueryValueW(buffer.data(), sub_block, &value, &value_length) == FALSE ||
        value == nullptr || value_length == 0)
    {
        return std::nullopt;
    }

    const std::string text =
        usefulMetadataText(juce::String{static_cast<const wchar_t*>(value)}.toStdString());
    return text.empty() ? std::optional<std::string>{} : std::optional<std::string>{text};
}

[[nodiscard]] std::vector<VersionResourceTranslation> versionResourceTranslations(
    const std::vector<std::byte>& buffer)
{
    std::vector<VersionResourceTranslation> translations;
    LPVOID value{};
    UINT value_length{};
    if (VerQueryValueW(buffer.data(), L"\\VarFileInfo\\Translation", &value, &value_length) !=
            FALSE &&
        value != nullptr)
    {
        const auto* const first = static_cast<const VersionResourceTranslation*>(value);
        const auto count = static_cast<std::size_t>(value_length) / sizeof(*first);
        translations.assign(first, first + count);
    }

    const auto append_unique = [&translations](const VersionResourceTranslation translation) {
        if (std::ranges::find(translations, translation) == translations.end())
        {
            translations.push_back(translation);
        }
    };
    append_unique(VersionResourceTranslation{.language = 0x0409, .code_page = 0x04b0});
    append_unique(VersionResourceTranslation{.language = 0x0409, .code_page = 0x04e4});
    return translations;
}

[[nodiscard]] std::optional<std::string> windowsVersionResourceCompanyName(
    const std::filesystem::path& plugin_path)
{
    std::error_code error;
    if (!std::filesystem::is_regular_file(plugin_path, error))
    {
        return std::nullopt;
    }

    DWORD unused_handle{};
    const DWORD buffer_size = GetFileVersionInfoSizeW(plugin_path.native().c_str(), &unused_handle);
    if (buffer_size == 0)
    {
        return std::nullopt;
    }

    std::vector<std::byte> buffer(buffer_size);
    if (GetFileVersionInfoW(plugin_path.native().c_str(), 0, buffer_size, buffer.data()) == FALSE)
    {
        return std::nullopt;
    }

    for (const VersionResourceTranslation translation : versionResourceTranslations(buffer))
    {
        if (std::optional<std::string> company_name =
                versionResourceString(buffer, translation, L"CompanyName");
            company_name.has_value())
        {
            return company_name;
        }
    }

    return std::nullopt;
}

[[nodiscard]] std::optional<std::string> windowsVst3FactoryManufacturer(
    const std::filesystem::path& plugin_path)
{
    std::error_code error;
    if (!std::filesystem::is_regular_file(plugin_path, error))
    {
        return std::nullopt;
    }

    using InitDllProc = bool(PLUGIN_API*)();
    using GetFactoryProc = Steinberg::IPluginFactory*(PLUGIN_API*)();
    WindowsVst3Module module{LoadLibraryW(plugin_path.native().c_str())};
    if (!module)
    {
        return std::nullopt;
    }

    if (const auto init_dll = module.function<InitDllProc>("InitDll");
        init_dll != nullptr && !init_dll())
    {
        return std::nullopt;
    }

    const auto get_factory = module.function<GetFactoryProc>("GetPluginFactory");
    if (get_factory == nullptr)
    {
        return std::nullopt;
    }

    std::unique_ptr<Steinberg::IPluginFactory, Vst3FactoryReleaser> factory{get_factory()};
    if (factory == nullptr)
    {
        return std::nullopt;
    }

    Steinberg::PFactoryInfo factory_info{};
    if (factory->getFactoryInfo(&factory_info) != Steinberg::kResultOk)
    {
        return std::nullopt;
    }

    const std::string manufacturer = fixedChar8MetadataText(
        factory_info.vendor, static_cast<std::size_t>(Steinberg::PFactoryInfo::kNameSize));
    return manufacturer.empty() ? std::optional<std::string>{}
                                : std::optional<std::string>{manufacturer};
}
#endif

[[nodiscard]] std::optional<PluginCatalogStaticMetadata> readVersionResourceMetadata(
    const std::filesystem::path& plugin_path,
    std::optional<PluginCatalogFileStamp> moduleinfo_file_stamp)
{
#if defined(_WIN32)
    std::optional<std::string> manufacturer = windowsVersionResourceCompanyName(plugin_path);
    if (!manufacturer.has_value())
    {
        return std::nullopt;
    }

    return PluginCatalogStaticMetadata{
        .name = {},
        .manufacturer = std::move(*manufacturer),
        .source = PluginCatalogMetadataSource::VersionResource,
        .moduleinfo_file_stamp = std::move(moduleinfo_file_stamp),
    };
#else
    static_cast<void>(plugin_path);
    static_cast<void>(moduleinfo_file_stamp);
    return std::nullopt;
#endif
}

[[nodiscard]] std::optional<PluginCatalogStaticMetadata> readVst3FactoryMetadata(
    const std::filesystem::path& plugin_path,
    std::optional<PluginCatalogFileStamp> moduleinfo_file_stamp)
{
#if defined(_WIN32)
    std::optional<std::string> manufacturer = windowsVst3FactoryManufacturer(plugin_path);
    if (!manufacturer.has_value())
    {
        return std::nullopt;
    }

    return PluginCatalogStaticMetadata{
        .name = {},
        .manufacturer = std::move(*manufacturer),
        .source = PluginCatalogMetadataSource::PluginFactory,
        .moduleinfo_file_stamp = std::move(moduleinfo_file_stamp),
    };
#else
    static_cast<void>(plugin_path);
    static_cast<void>(moduleinfo_file_stamp);
    return std::nullopt;
#endif
}

[[nodiscard]] std::optional<PluginCatalogStaticMetadata> readVst3FallbackMetadata(
    const std::filesystem::path& plugin_path,
    const std::optional<PluginCatalogFileStamp>& moduleinfo_file_stamp)
{
    if (std::optional<PluginCatalogStaticMetadata> version_metadata =
            readVersionResourceMetadata(plugin_path, moduleinfo_file_stamp);
        version_metadata.has_value())
    {
        return version_metadata;
    }

    return readVst3FactoryMetadata(plugin_path, moduleinfo_file_stamp);
}

} // namespace

std::string pluginCatalogPathToUtf8String(const std::filesystem::path& path)
{
    const std::u8string encoded = path.u8string();
    std::string result(encoded.size(), '\0');
    std::ranges::transform(
        encoded, result.begin(), [](char8_t byte) { return std::bit_cast<char>(byte); });
    return result;
}

std::expected<PluginCatalogDocument, PluginCatalogCacheError> loadPluginCatalogCache(
    const std::filesystem::path& cache_file)
{
    std::error_code error;
    if (!std::filesystem::exists(cache_file, error))
    {
        return PluginCatalogDocument{};
    }
    if (!std::filesystem::is_regular_file(cache_file, error))
    {
        return std::unexpected{makeCacheError(
            PluginCatalogCacheError::Code::CouldNotRead,
            "Plugin catalog cache is not a regular file")};
    }

    std::ifstream file{cache_file, std::ios::binary};
    if (!file.is_open())
    {
        return std::unexpected{makeCacheError(
            PluginCatalogCacheError::Code::CouldNotRead,
            "Could not open plugin catalog cache for reading")};
    }

    std::ostringstream contents;
    contents << file.rdbuf();
    const std::string json_text = contents.str();
    auto document_value = common::core::Json::parseUtf8Document(json_text);
    if (!document_value.has_value() || !document_value->isObject())
    {
        return std::unexpected{makeCacheError(
            PluginCatalogCacheError::Code::InvalidJson,
            document_value.has_value() ? "Plugin catalog cache root is not an object"
                                       : document_value.error().message)};
    }

    const int schema_version =
        common::core::Json::readOptionalInt(*document_value, "schema_version", -1);
    if (schema_version != g_plugin_catalog_cache_schema_version)
    {
        return std::unexpected{makeCacheError(
            PluginCatalogCacheError::Code::UnsupportedSchema,
            "Plugin catalog cache schema is not supported")};
    }

    const juce::var& records_value = common::core::Json::value(*document_value, "records");
    if (!records_value.isArray())
    {
        return std::unexpected{makeCacheError(
            PluginCatalogCacheError::Code::InvalidJson,
            "Plugin catalog cache records are not an array")};
    }

    PluginCatalogDocument document;
    const juce::Array<juce::var>* const records = records_value.getArray();
    document.records.reserve(static_cast<std::size_t>(records->size()));
    for (const juce::var& record_value : *records)
    {
        auto record = readPluginCatalogRecord(record_value);
        if (!record.has_value())
        {
            return std::unexpected{std::move(record.error())};
        }

        document.records.push_back(std::move(*record));
    }

    return document;
}

std::expected<void, PluginCatalogCacheError> savePluginCatalogCache(
    const std::filesystem::path& cache_file, const PluginCatalogDocument& document)
{
    std::error_code error;
    const std::filesystem::path parent_path = cache_file.parent_path();
    if (!parent_path.empty())
    {
        std::filesystem::create_directories(parent_path, error);
        if (error)
        {
            return std::unexpected{makeCacheError(
                PluginCatalogCacheError::Code::CouldNotWrite,
                "Could not create plugin catalog cache directory")};
        }
    }

    std::ofstream file{cache_file, std::ios::binary | std::ios::trunc};
    if (!file.is_open())
    {
        return std::unexpected{makeCacheError(
            PluginCatalogCacheError::Code::CouldNotWrite,
            "Could not open plugin catalog cache for writing")};
    }

    file << juce::JSON::toString(makePluginCatalogDocumentJson(document)).toStdString() << '\n';
    if (!file.good())
    {
        return std::unexpected{makeCacheError(
            PluginCatalogCacheError::Code::CouldNotWrite, "Could not write plugin catalog cache")};
    }

    return {};
}

std::optional<PluginCatalogFileStamp> pluginCatalogFileStamp(const std::filesystem::path& path)
{
    std::error_code error;
    if (!std::filesystem::exists(path, error))
    {
        return std::nullopt;
    }

    const std::filesystem::file_time_type last_write_time =
        std::filesystem::last_write_time(path, error);
    if (error)
    {
        return std::nullopt;
    }

    std::uintmax_t size_bytes{};
    if (std::filesystem::is_regular_file(path, error))
    {
        size_bytes = std::filesystem::file_size(path, error);
        if (error)
        {
            return std::nullopt;
        }
    }

    const auto time_count = last_write_time.time_since_epoch().count();
    if (time_count > std::numeric_limits<std::int64_t>::max() ||
        time_count < std::numeric_limits<std::int64_t>::min())
    {
        return std::nullopt;
    }

    return PluginCatalogFileStamp{
        .size_bytes = size_bytes,
        .last_write_time = static_cast<std::int64_t>(time_count),
    };
}

bool isPluginCatalogRecordCurrent(
    const PluginCatalogRecord& record,
    const std::optional<PluginCatalogFileStamp>& module_file_stamp,
    const std::optional<PluginCatalogFileStamp>& moduleinfo_file_stamp)
{
    return record.module_file_stamp == module_file_stamp &&
           record.moduleinfo_file_stamp == moduleinfo_file_stamp;
}

std::optional<std::filesystem::path> findVst3ModuleInfoPath(
    const std::filesystem::path& plugin_path)
{
    const std::optional<std::filesystem::path> bundle_path = vst3BundleRootForPath(plugin_path);
    if (!bundle_path.has_value())
    {
        return std::nullopt;
    }

    return existingModuleInfoPathInBundle(*bundle_path);
}

std::optional<PluginCatalogStaticMetadata> readVst3StaticMetadata(
    const std::filesystem::path& plugin_path)
{
    const std::optional<std::filesystem::path> moduleinfo_path =
        findVst3ModuleInfoPath(plugin_path);
    const std::optional<PluginCatalogFileStamp> moduleinfo_stamp =
        moduleinfo_path.has_value() ? pluginCatalogFileStamp(*moduleinfo_path)
                                    : std::optional<PluginCatalogFileStamp>{};
    if (!moduleinfo_path.has_value())
    {
        return readVst3FallbackMetadata(plugin_path, moduleinfo_stamp);
    }

    const std::string json_text = readFileToString(*moduleinfo_path);
    const std::optional<Steinberg::ModuleInfo> module_info =
        Steinberg::ModuleInfoLib::parseJson(json_text, nullptr);
    if (!module_info.has_value())
    {
        return readVst3FallbackMetadata(plugin_path, moduleinfo_stamp);
    }

    std::vector<Steinberg::ModuleInfo::ClassInfo> audio_classes;
    for (const Steinberg::ModuleInfo::ClassInfo& class_info : module_info->classes)
    {
        if (class_info.category == g_vst_audio_effect_class)
        {
            audio_classes.push_back(class_info);
        }
    }

    std::string manufacturer = moduleInfoManufacturer(*module_info, audio_classes);
    PluginCatalogMetadataSource source = PluginCatalogMetadataSource::ModuleInfo;
    if (manufacturer.empty())
    {
        if (std::optional<PluginCatalogStaticMetadata> fallback_metadata =
                readVst3FallbackMetadata(plugin_path, moduleinfo_stamp);
            fallback_metadata.has_value())
        {
            manufacturer = std::move(fallback_metadata->manufacturer);
            source = fallback_metadata->source;
        }
    }

    const std::string name =
        audio_classes.size() == 1 ? usefulMetadataText(audio_classes.front().name) : std::string{};
    if (name.empty() && manufacturer.empty())
    {
        return readVst3FallbackMetadata(plugin_path, moduleinfo_stamp);
    }

    return PluginCatalogStaticMetadata{
        .name = name,
        .manufacturer = std::move(manufacturer),
        .source = source,
        .moduleinfo_file_stamp = moduleinfo_stamp,
    };
}

} // namespace rock_hero::common::audio
