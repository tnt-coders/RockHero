#include "plugin_catalog_cache.h"

#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>

namespace rock_hero::common::audio
{

namespace
{

// Owns a clean temporary directory for plugin catalog cache fixtures.
class TemporaryPluginCatalogDirectory final
{
public:
    // Creates a test-local directory under the platform temp root.
    TemporaryPluginCatalogDirectory()
        : m_path(
              std::filesystem::temp_directory_path() /
              ("rock-hero-plugin-catalog-test-" +
               std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())))
    {
        std::filesystem::remove_all(m_path);
        std::filesystem::create_directories(m_path);
    }

    // Removes the temp directory on a best-effort basis.
    ~TemporaryPluginCatalogDirectory() noexcept
    {
        try
        {
            std::error_code error;
            std::filesystem::remove_all(m_path, error);
        }
        catch (...)
        {
            m_path.clear();
        }
    }

    TemporaryPluginCatalogDirectory(const TemporaryPluginCatalogDirectory&) = delete;
    TemporaryPluginCatalogDirectory& operator=(const TemporaryPluginCatalogDirectory&) = delete;
    TemporaryPluginCatalogDirectory(TemporaryPluginCatalogDirectory&&) = delete;
    TemporaryPluginCatalogDirectory& operator=(TemporaryPluginCatalogDirectory&&) = delete;

    // Returns the temporary root used by this test.
    [[nodiscard]] const std::filesystem::path& path() const noexcept
    {
        return m_path;
    }

private:
    // Temporary root removed by the destructor after each test.
    std::filesystem::path m_path;
};

void writeTextFile(const std::filesystem::path& path, std::string_view contents)
{
    std::filesystem::create_directories(path.parent_path());
    std::ofstream file{path, std::ios::binary};
    REQUIRE(file.is_open());
    file << contents;
}

[[nodiscard]] std::string moduleInfoJson(std::string_view class_entries)
{
    return std::string{
               R"json({
  "Name": "Rock Hero Test Bundle",
  "Version": "1.0.0",
  "Factory Info": {
    "Vendor": "Acme Audio",
    "URL": "https://example.test",
    "E-Mail": "support@example.test",
    "Flags": {
      "Unicode": true,
      "Classes Discardable": false,
      "Component Non Discardable": false,
    },
  },
  "Classes": [
)json"
           } +
           std::string{class_entries} +
           R"json(
  ],
  "Compatibility": [],
})json";
}

[[nodiscard]] std::string audioClassJson(
    std::string_view cid, std::string_view name, bool add_trailing_comma)
{
    return std::string{
               R"json(    {
      "CID": ")json"
           } +
           std::string{cid} +
           R"json(",
      "Category": "Audio Module Class",
      "Name": ")json" +
           std::string{name} +
           R"json(",
      "Vendor": "Acme Audio",
      "Version": "1.0.0",
      "SDKVersion": "VST 3.7.4",
      "Sub Categories": [
        "Fx",
      ],
      "Class Flags": 0,
      "Cardinality": 2147483647,
      "Snapshots": [],
    })json" +
           (add_trailing_comma ? "," : "");
}

[[nodiscard]] std::filesystem::path createVst3BundleWithModuleInfo(
    const std::filesystem::path& root, std::string_view moduleinfo_json)
{
    const std::filesystem::path bundle_path = root / "Acme Amp.vst3";
    writeTextFile(bundle_path / "Contents" / "Resources" / "moduleinfo.json", moduleinfo_json);
    writeTextFile(bundle_path / "Contents" / "x86_64-win" / "Acme Amp.vst3", "module-binary");
    return bundle_path;
}

} // namespace

// Verifies a missing cache file behaves like an empty catalog.
TEST_CASE("Plugin catalog cache missing file loads empty", "[audio][plugin-catalog]")
{
    const TemporaryPluginCatalogDirectory temporary_directory;

    const auto cache =
        loadPluginCatalogCache(temporary_directory.path() / "PluginCatalogCache.json");

    REQUIRE(cache.has_value());
    CHECK(cache->records.empty());
}

// Verifies malformed JSON is reported as a recoverable cache parse failure.
TEST_CASE("Plugin catalog cache rejects malformed JSON", "[audio][plugin-catalog]")
{
    const TemporaryPluginCatalogDirectory temporary_directory;
    const std::filesystem::path cache_path = temporary_directory.path() / "PluginCatalogCache.json";
    writeTextFile(cache_path, "{not-json");

    const auto cache = loadPluginCatalogCache(cache_path);

    REQUIRE_FALSE(cache.has_value());
    CHECK(cache.error().code == PluginCatalogCacheError::Code::InvalidJson);
}

// Verifies unsupported schemas are ignored instead of partially migrated.
TEST_CASE("Plugin catalog cache rejects unknown schema", "[audio][plugin-catalog]")
{
    const TemporaryPluginCatalogDirectory temporary_directory;
    const std::filesystem::path cache_path = temporary_directory.path() / "PluginCatalogCache.json";
    writeTextFile(cache_path, R"json({"schema_version":999,"records":[]})json");

    const auto cache = loadPluginCatalogCache(cache_path);

    REQUIRE_FALSE(cache.has_value());
    CHECK(cache.error().code == PluginCatalogCacheError::Code::UnsupportedSchema);
}

// Verifies cache records round-trip without losing display metadata or stamps.
TEST_CASE("Plugin catalog cache round trips records", "[audio][plugin-catalog]")
{
    const TemporaryPluginCatalogDirectory temporary_directory;
    const std::filesystem::path cache_path = temporary_directory.path() / "PluginCatalogCache.json";
    const PluginCatalogRecord record{
        .path_key = "c:/plugins/acme amp.vst3",
        .file_path = std::filesystem::path{R"(C:\Plugins\Acme Amp.vst3)"},
        .format_name = "VST3",
        .name = "Acme Amp",
        .manufacturer = "Acme Audio",
        .metadata_source = PluginCatalogMetadataSource::ModuleInfo,
        .module_file_stamp = PluginCatalogFileStamp{.size_bytes = 42, .last_write_time = 1001},
        .moduleinfo_file_stamp = PluginCatalogFileStamp{.size_bytes = 24, .last_write_time = 1002},
    };
    const PluginCatalogRecord filename_record{
        .path_key = "c:/plugins/legacy drive.vst3",
        .file_path = std::filesystem::path{R"(C:\Plugins\Legacy Drive.vst3)"},
        .format_name = "VST3",
        .name = "Legacy Drive",
        .manufacturer = {},
        .metadata_source = PluginCatalogMetadataSource::Filename,
        .module_file_stamp = PluginCatalogFileStamp{.size_bytes = 52, .last_write_time = 1003},
        .moduleinfo_file_stamp = std::nullopt,
    };
    const PluginCatalogRecord path_record{
        .path_key = "c:/plugins/acme/legacy cab.vst3",
        .file_path = std::filesystem::path{R"(C:\Plugins\Acme\Legacy Cab.vst3)"},
        .format_name = "VST3",
        .name = "Legacy Cab",
        .manufacturer = "Acme",
        .metadata_source = PluginCatalogMetadataSource::Path,
        .module_file_stamp = PluginCatalogFileStamp{.size_bytes = 62, .last_write_time = 1004},
        .moduleinfo_file_stamp = std::nullopt,
    };
    const PluginCatalogRecord factory_record{
        .path_key = "c:/plugins/acme/factory drive.vst3",
        .file_path = std::filesystem::path{R"(C:\Plugins\Acme\Factory Drive.vst3)"},
        .format_name = "VST3",
        .name = "Factory Drive",
        .manufacturer = "Acme Audio",
        .metadata_source = PluginCatalogMetadataSource::PluginFactory,
        .module_file_stamp = PluginCatalogFileStamp{.size_bytes = 72, .last_write_time = 1005},
        .moduleinfo_file_stamp = std::nullopt,
    };

    REQUIRE(savePluginCatalogCache(
                cache_path,
                PluginCatalogDocument{
                    .records = {record, filename_record, path_record, factory_record}
                })
                .has_value());
    const auto cache = loadPluginCatalogCache(cache_path);

    REQUIRE(cache.has_value());
    REQUIRE(cache->records.size() == 4);
    CHECK(cache->records.front().path_key == record.path_key);
    CHECK(cache->records.front().file_path == record.file_path);
    CHECK(cache->records.front().name == record.name);
    CHECK(cache->records.front().manufacturer == record.manufacturer);
    CHECK(cache->records.front().metadata_source == PluginCatalogMetadataSource::ModuleInfo);
    CHECK(cache->records.front().module_file_stamp == record.module_file_stamp);
    CHECK(cache->records.front().moduleinfo_file_stamp == record.moduleinfo_file_stamp);
    CHECK(cache->records[1].moduleinfo_file_stamp == std::nullopt);
    CHECK(cache->records[2].manufacturer == path_record.manufacturer);
    CHECK(cache->records[2].metadata_source == PluginCatalogMetadataSource::Path);
    CHECK(cache->records[2].moduleinfo_file_stamp == std::nullopt);
    CHECK(cache->records.back().manufacturer == factory_record.manufacturer);
    CHECK(cache->records.back().metadata_source == PluginCatalogMetadataSource::PluginFactory);
    CHECK(cache->records.back().moduleinfo_file_stamp == std::nullopt);
}

// Verifies module and moduleinfo stamps both participate in cache invalidation.
TEST_CASE("Plugin catalog cache checks moduleinfo stamp", "[audio][plugin-catalog]")
{
    const PluginCatalogRecord record{
        .path_key = "amp",
        .file_path = std::filesystem::path{"Amp.vst3"},
        .module_file_stamp = PluginCatalogFileStamp{.size_bytes = 10, .last_write_time = 20},
        .moduleinfo_file_stamp = PluginCatalogFileStamp{.size_bytes = 11, .last_write_time = 21},
    };

    CHECK(isPluginCatalogRecordCurrent(
        record,
        PluginCatalogFileStamp{.size_bytes = 10, .last_write_time = 20},
        PluginCatalogFileStamp{.size_bytes = 11, .last_write_time = 21}));
    CHECK_FALSE(isPluginCatalogRecordCurrent(
        record,
        PluginCatalogFileStamp{.size_bytes = 10, .last_write_time = 20},
        PluginCatalogFileStamp{.size_bytes = 12, .last_write_time = 21}));
}

// Verifies Windows-normalized VST3 module paths still find bundle moduleinfo.
TEST_CASE("Plugin catalog finds moduleinfo from module path", "[audio][plugin-catalog]")
{
    const TemporaryPluginCatalogDirectory temporary_directory;
    const std::filesystem::path bundle_path = createVst3BundleWithModuleInfo(
        temporary_directory.path(),
        moduleInfoJson(audioClassJson("11111111111111111111111111111111", "Acme Amp", false)));
    const std::filesystem::path module_path =
        bundle_path / "Contents" / "x86_64-win" / "Acme Amp.vst3";

    const auto moduleinfo_path = findVst3ModuleInfoPath(module_path);

    REQUIRE(moduleinfo_path.has_value());
    CHECK(moduleinfo_path->filename() == "moduleinfo.json");
}

// Verifies single-class moduleinfo supplies both display name and manufacturer.
TEST_CASE("Plugin catalog reads single class moduleinfo", "[audio][plugin-catalog]")
{
    const TemporaryPluginCatalogDirectory temporary_directory;
    const std::filesystem::path bundle_path = createVst3BundleWithModuleInfo(
        temporary_directory.path(),
        moduleInfoJson(audioClassJson("11111111111111111111111111111111", "Acme Amp", false)));

    const auto metadata = readVst3StaticMetadata(bundle_path);

    REQUIRE(metadata.has_value());
    CHECK(metadata->name == "Acme Amp");
    CHECK(metadata->manufacturer == "Acme Audio");
    CHECK(metadata->source == PluginCatalogMetadataSource::ModuleInfo);
    CHECK(metadata->moduleinfo_file_stamp.has_value());
}

// Verifies multi-class bundles keep filename-derived names for later class-level selection.
TEST_CASE("Plugin catalog keeps multi class name empty", "[audio][plugin-catalog]")
{
    const TemporaryPluginCatalogDirectory temporary_directory;
    const std::filesystem::path bundle_path = createVst3BundleWithModuleInfo(
        temporary_directory.path(),
        moduleInfoJson(
            audioClassJson("11111111111111111111111111111111", "Acme Amp", true) + "\n" +
            audioClassJson("22222222222222222222222222222222", "Acme Cab", false)));

    const auto metadata = readVst3StaticMetadata(bundle_path);

    REQUIRE(metadata.has_value());
    CHECK(metadata->name.empty());
    CHECK(metadata->manufacturer == "Acme Audio");
    CHECK(metadata->source == PluginCatalogMetadataSource::ModuleInfo);
}

// Verifies moduleinfo factory metadata is still useful when classes cannot name a plugin row.
TEST_CASE("Plugin catalog keeps vendor without audio classes", "[audio][plugin-catalog]")
{
    const TemporaryPluginCatalogDirectory temporary_directory;
    const std::filesystem::path bundle_path =
        createVst3BundleWithModuleInfo(temporary_directory.path(), moduleInfoJson(R"json(    {
      "CID": "33333333333333333333333333333333",
      "Category": "Component Controller Class",
      "Name": "Acme Controller",
      "Vendor": "Acme Audio",
      "Version": "1.0.0",
      "SDKVersion": "VST 3.7.4",
      "Sub Categories": [],
      "Class Flags": 0,
      "Cardinality": 2147483647,
      "Snapshots": [],
    })json"));

    const auto metadata = readVst3StaticMetadata(bundle_path);

    REQUIRE(metadata.has_value());
    CHECK(metadata->name.empty());
    CHECK(metadata->manufacturer == "Acme Audio");
    CHECK(metadata->source == PluginCatalogMetadataSource::ModuleInfo);
}

} // namespace rock_hero::common::audio
