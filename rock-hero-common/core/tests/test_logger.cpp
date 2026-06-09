#include <catch2/catch_test_macros.hpp>
#include <rock_hero/common/core/logger.h>

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

// Verifies the project macro wrapper does not shadow a caller variable named `logger` and does not
// evaluate arguments after shutdown.
TEST_CASE("Log macros preserve caller scope and backend lifetime", "[core][logging]")
{
    const Logger::InitResult init_result = Logger::init(Logger::Config{});
    REQUIRE(init_result.backend_started);

    const int logger = 7;
    int observed = 0;

    RH_LOG_INFO("test.logger_macro", "caller logger value={}", (observed = logger));
    CHECK(observed == 7);

    Logger::shutdown();

    RH_LOG_CRITICAL("test.logger_macro", "skipped value={}", (observed = 99));
    CHECK(observed == 7);
}

} // namespace rock_hero::common::core
