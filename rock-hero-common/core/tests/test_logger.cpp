#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <expected>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <rock_hero/common/core/infrastructure/logger.h>
#include <string>
#include <string_view>
#include <system_error>

namespace rock_hero::common::core
{

// Well-formed categories validate at compile time and are usable as constant expressions, so the
// category name baked into each logger cannot drift into an unvalidated string. Ill-formed
// categories (uppercase, spaces, empty segments) are rejected by the consteval constructor and are
// therefore not expressible here -- that rejection is a compile error, not a runtime check.
static_assert(Logger::Category{"editor.app"}.name() == "editor.app");
static_assert(Logger::Category{"audio.plugin_validation"}.name() == "audio.plugin_validation");
static_assert(
    Logger::Category{"audio.plugin-catalog_scan.detail"}.name() ==
    "audio.plugin-catalog_scan.detail");
static_assert(Logger::RealtimeFrontendOptions::queue_type == quill::QueueType::BoundedDropping);
static_assert(Logger::RealtimeFrontendOptions::blocking_queue_retry_interval_ns == 0);

// Verifies the validated category text survives unchanged into the runtime accessor.
TEST_CASE("Log category preserves its validated name", "[core][logging]")
{
    CHECK(Logger::Category{"editor.controller"}.name() == "editor.controller");
}

// Verifies the `{:?}` field convention renders quoted, escaped string values through a real backend
// record, and that the macro wrapper neither shadows a caller variable named `logger` nor evaluates
// arguments after shutdown. The backend allows a single init/shutdown cycle per process, so these
// backend-dependent checks share one cycle.
TEST_CASE("Log macros quote string fields and guard caller scope and lifetime", "[core][logging]")
{
    namespace fs = std::filesystem;
    const auto seed = std::chrono::steady_clock::now().time_since_epoch().count();
    fs::path test_directory;
    for (std::size_t attempt = 0; attempt < 100U; ++attempt)
    {
        const fs::path candidate =
            fs::temp_directory_path() /
            ("rock-hero-logger-test-" + std::to_string(seed) + "-" + std::to_string(attempt));
        std::error_code create_error;
        if (fs::create_directory(candidate, create_error))
        {
            test_directory = candidate;
            break;
        }
    }
    REQUIRE_FALSE(test_directory.empty());

    const fs::path log_file = test_directory / "logger.log";

    Logger::Config config;
    config.log_file = log_file;
    const std::expected<void, LoggerError> init_result = Logger::init(config);
    REQUIRE(init_result.has_value());

    const int logger = 7;
    int observed = 0;

    RH_LOG_INFO("test.logger_macro", "caller logger value={}", (observed = logger));
    CHECK(observed == 7);

    // `{:?}` quotes and escapes string values so whitespace and delimiters stay unambiguous, while
    // `{}` leaves scalars bare. This also guards that Quill's bundled fmt still supports `{:?}`.
    RH_LOG_INFO("test.logger_macro", "label={:?} count={}", std::string{"two words"}, 3);
    RH_LOG_INFO("test.logger_macro", "view={:?}", std::string_view{"view words"});
    RH_LOG_INFO("test.logger_macro", "raw={:?}", std::string{"tab\tquote\""});

    Logger::shutdown();

    RH_LOG_CRITICAL("test.logger_macro", "skipped value={}", (observed = 99));
    CHECK(observed == 7);

    std::ifstream stream{log_file};
    REQUIRE(stream.is_open());
    const std::string contents{
        std::istreambuf_iterator<char>{stream}, std::istreambuf_iterator<char>{}
    };
    stream.close();

    CHECK(contents.find("label=\"two words\" count=3") != std::string::npos);
    CHECK(contents.find("view=\"view words\"") != std::string::npos);
    CHECK(contents.find("raw=\"tab\\tquote\\\"\"") != std::string::npos);

    std::error_code remove_error;
    fs::remove_all(test_directory, remove_error);
}

} // namespace rock_hero::common::core
