#include "log_capture.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/reporters/catch_reporter_event_listener.hpp>
#include <catch2/reporters/catch_reporter_registrars.hpp>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <ios>
#include <iterator>
#include <limits>
#include <rock_hero/common/core/shared/logger.h>
#include <string>
#include <system_error>

namespace rock_hero::editor::core::testing
{

namespace
{

// The directory this process's log lives in, created on first use under a name no other process
// holds; empty when no such directory could be made.
[[nodiscard]] const std::filesystem::path& processLogDirectory()
{
    static const std::filesystem::path g_directory = [] {
        std::filesystem::path made;
        const auto seed = std::chrono::steady_clock::now().time_since_epoch().count();
        for (std::size_t attempt = 0; attempt < 100U && made.empty(); ++attempt)
        {
            const std::filesystem::path candidate =
                std::filesystem::temp_directory_path() /
                ("rock-hero-editor-core-log-" + std::to_string(seed) + "-" +
                 std::to_string(attempt));
            std::error_code error;
            if (std::filesystem::create_directory(candidate, error))
            {
                made = candidate;
            }
        }
        return made;
    }();
    return g_directory;
}

void removeProcessLogDirectory()
{
    std::error_code error;
    std::filesystem::remove_all(processLogDirectory(), error);
}

// The process's log file, the backend started on it at first use. A capture reads by byte offset,
// so the file must never rotate under it: the rotation size is set past anything a run can write.
[[nodiscard]] const std::filesystem::path& processLogFile()
{
    static const std::filesystem::path g_log_file = [] {
        const std::filesystem::path& directory = processLogDirectory();
        if (directory.empty())
        {
            return std::filesystem::path{};
        }
        // The file stays open until Quill's own singletons are destroyed at exit, and Windows
        // cannot delete an open file, so the directory goes in an exit handler. Registered before
        // init creates those singletons, it runs after their destructors; registered after the
        // directory's own static, it runs before that path is destroyed.
        static_cast<void>(std::atexit(removeProcessLogDirectory));
        common::core::Logger::Config config;
        config.log_file = directory / "editor.log";
        config.max_file_size_bytes = std::numeric_limits<std::size_t>::max();
        if (!common::core::Logger::init(config).has_value())
        {
            return std::filesystem::path{};
        }
        return config.log_file;
    }();
    REQUIRE_FALSE(g_log_file.empty());
    return g_log_file;
}

// The log file's size once everything logged so far is in it: the backend writes on its own thread,
// and a flush returns only once it has written every record logged before the flush.
[[nodiscard]] std::uintmax_t flushedLogSize(const std::filesystem::path& log_file)
{
    common::core::Logger::get(common::core::Logger::Category{"test.log_capture"})->flush_log();
    std::error_code error;
    const std::uintmax_t size = std::filesystem::file_size(log_file, error);
    REQUIRE_FALSE(error);
    return size;
}

// Stops the backend once the whole run has ended, as the editor's main does before it returns, so
// the JUCE log bridge is gone before static teardown begins. Shutdown is a no-op for a run that
// never captured.
class ProcessLogShutdown final : public Catch::EventListenerBase
{
public:
    using Catch::EventListenerBase::EventListenerBase;

    void testRunEnded(const Catch::TestRunStats& /*stats*/) override
    {
        common::core::Logger::shutdown();
    }
};

} // namespace

CATCH_REGISTER_LISTENER(ProcessLogShutdown)

LogCapture::LogCapture()
    : m_start{flushedLogSize(processLogFile())}
{}

std::string LogCapture::text() const
{
    const std::filesystem::path& log_file = processLogFile();
    const std::uintmax_t end = flushedLogSize(log_file);
    REQUIRE(end >= m_start);
    std::ifstream stream{log_file, std::ios::binary};
    REQUIRE(stream.is_open());
    stream.seekg(static_cast<std::streamoff>(m_start));
    return std::string{std::istreambuf_iterator<char>{stream}, std::istreambuf_iterator<char>{}};
}

} // namespace rock_hero::editor::core::testing
