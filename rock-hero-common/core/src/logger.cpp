#include "logger.h"

#include <atomic>
#include <cstdint>
#include <exception>
#include <expected>
#include <filesystem>
#include <juce_core/juce_core.h>
#include <memory>
#include <mutex>
#include <quill/Backend.h>
#include <quill/Frontend.h>
#include <quill/core/PatternFormatterOptions.h>
#include <quill/sinks/ConsoleSink.h>
#include <quill/sinks/NullSink.h>
#include <quill/sinks/RotatingFileSink.h>
#include <string>
#include <utility>
#include <vector>

namespace rock_hero::common::core
{

namespace
{

using RealtimeFrontend = quill::FrontendImpl<Logger::RealtimeFrontendOptions>;

constexpr std::string_view g_realtime_logger_prefix{"rt."};

// Bridges JUCE and Tracktion diagnostics into the same Quill pipeline as project records, so a
// single log file can hold both framework and application output.
class JuceQuillBridge final : public juce::Logger
{
public:
    explicit JuceQuillBridge(::rock_hero::common::core::Logger::Handle logger)
        : m_logger{logger}
    {}

    // JUCE owns the text by the time it reaches this bridge; Quill defers final formatting.
    void logMessage(const juce::String& message) override
    {
        if (::rock_hero::common::core::Logger::isStarted())
        {
            QUILL_LOG_INFO(m_logger, "{}", message.toRawUTF8());
        }
    }

private:
    ::rock_hero::common::core::Logger::Handle m_logger;
};

// Process-wide logging state. Held behind a function-local static accessor rather than namespace
// globals so it stays out of cppcoreguidelines-avoid-non-const-global-variables while still living
// for the process lifetime.
struct LoggingState
{
    // Guards the configured sink list because category loggers can first be requested by workers.
    std::mutex sinks_mutex;

    // Sinks created during Logger::init and shared by every category logger.
    std::vector<std::shared_ptr<quill::Sink>> sinks;

    // Owns the installed JUCE bridge for the lifetime of visible logging sinks.
    std::unique_ptr<JuceQuillBridge> juce_bridge;

    // Quill's backend can be started once per process; a stopped backend is not restarted.
    bool backend_started_once = false;

    // Tracks backend lifetime for log macros without taking a lock on the caller thread.
    std::atomic_bool backend_started = false;

    // Last startup result, returned unchanged if init is called again while logging is active.
    Logger::InitResult last_init_result;
};

[[nodiscard]] LoggingState& loggingState()
{
    static LoggingState state;
    return state;
}

[[nodiscard]] quill::PatternFormatterOptions logPattern()
{
    return quill::PatternFormatterOptions{
        "%(time) [%(log_level)] %(logger): %(message)",
        "%Y-%m-%dT%H:%M:%S.%Qms%z",
    };
}

[[nodiscard]] std::string categoryText(Logger::Category category)
{
    return std::string{category.name()};
}

[[nodiscard]] std::string realtimeCategoryText(Logger::Category category)
{
    std::string name{g_realtime_logger_prefix};
    name += category.name();
    return name;
}

[[nodiscard]] std::string exceptionText(const std::exception& exception)
{
    return exception.what();
}

[[nodiscard]] std::string exceptionText(...)
{
    return "unknown logging backend error";
}

[[nodiscard]] std::vector<std::shared_ptr<quill::Sink>> currentSinks()
{
    LoggingState& state = loggingState();
    const std::lock_guard<std::mutex> lock{state.sinks_mutex};
    return state.sinks;
}

void installSinks(std::vector<std::shared_ptr<quill::Sink>> sinks)
{
    LoggingState& state = loggingState();
    const std::lock_guard<std::mutex> lock{state.sinks_mutex};
    state.sinks = std::move(sinks);
}

[[nodiscard]] std::expected<std::shared_ptr<quill::Sink>, std::string> createFileSink(
    const Logger::Config& config)
{
    const std::filesystem::path parent_directory = config.log_file.parent_path();
    if (!parent_directory.empty())
    {
        std::error_code error;
        std::filesystem::create_directories(parent_directory, error);
        if (error)
        {
            return std::unexpected{"could not create log directory: " + error.message()};
        }
    }

    try
    {
        quill::RotatingFileSinkConfig file_config;
        file_config.set_open_mode('a');
        file_config.set_rotation_max_file_size(config.max_file_size_bytes);
        file_config.set_max_backup_files(static_cast<std::uint32_t>(config.max_backup_files));
        return quill::Frontend::create_or_get_sink<quill::RotatingFileSink>(
            config.log_file.string(), file_config);
    }
    catch (const std::exception& exception)
    {
        return std::unexpected{exceptionText(exception)};
    }
    catch (...)
    {
        return std::unexpected{exceptionText()};
    }
}

} // namespace

// Starts the async backend, installs shared sinks, and bridges JUCE output when there is somewhere
// visible to write it.
Logger::InitResult Logger::init(const Config& config)
{
    LoggingState& state = loggingState();
    if (state.backend_started.load(std::memory_order_acquire))
    {
        return state.last_init_result;
    }

    if (state.backend_started_once)
    {
        return InitResult{
            .failure_message = "logging backend cannot be restarted after shutdown",
        };
    }

    InitResult result;

    try
    {
        quill::Backend::start();
        state.backend_started_once = true;
        result.backend_started = true;
    }
    catch (const std::exception& exception)
    {
        result.failure_message = exceptionText(exception);
        return result;
    }
    catch (...)
    {
        result.failure_message = exceptionText();
        return result;
    }

    std::vector<std::shared_ptr<quill::Sink>> sinks;
    bool has_visible_sink = false;

    if (!config.log_file.empty())
    {
        auto file_sink = createFileSink(config);
        if (file_sink.has_value())
        {
            sinks.push_back(std::move(*file_sink));
            result.file_sink_active = true;
            has_visible_sink = true;
        }
        else
        {
            result.failure_message = file_sink.error();
            juce::Logger::writeToLog(
                "Rock Hero logging file sink could not be opened: " +
                juce::String::fromUTF8(result.failure_message.c_str()));
        }
    }

    if (config.log_to_console)
    {
        sinks.push_back(quill::Frontend::create_or_get_sink<quill::ConsoleSink>("console"));
        has_visible_sink = true;
    }

    if (sinks.empty())
    {
        sinks.push_back(quill::Frontend::create_or_get_sink<quill::NullSink>("rock-hero-null"));
    }

    installSinks(std::move(sinks));
    state.last_init_result = result;
    state.backend_started.store(true, std::memory_order_release);

    if (has_visible_sink)
    {
        state.juce_bridge = std::make_unique<JuceQuillBridge>(get(Category{"juce"}));
        juce::Logger::setCurrentLogger(state.juce_bridge.get());
    }

    return result;
}

// Removes the JUCE bridge, then flushes and stops the backend so no record is lost at shutdown.
void Logger::shutdown()
{
    LoggingState& state = loggingState();
    if (!state.backend_started.exchange(false, std::memory_order_acq_rel))
    {
        return;
    }

    juce::Logger::setCurrentLogger(nullptr);
    state.juce_bridge.reset();

    quill::Backend::stop();

    const std::lock_guard<std::mutex> lock{state.sinks_mutex};
    state.sinks.clear();
}

bool Logger::isStarted() noexcept
{
    return loggingState().backend_started.load(std::memory_order_acquire);
}

// Pre-allocates the bounded realtime frontend queue for the current thread.
void Logger::prepareRealtimeThread()
{
    RealtimeFrontend::preallocate();
}

// Creates the named normal logger on first request and returns the backend-cached handle thereafter.
Logger::Handle Logger::get(Category category)
{
    return quill::Frontend::create_or_get_logger(
        categoryText(category), currentSinks(), logPattern());
}

// Creates the named realtime logger with a bounded-dropping frontend queue.
Logger::RealtimeHandle Logger::getRealtime(Category category)
{
    return RealtimeFrontend::create_or_get_logger(
        realtimeCategoryText(category), currentSinks(), logPattern());
}

} // namespace rock_hero::common::core
