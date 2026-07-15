#include "shared/logger.h"

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
#include <rock_hero/common/core/shared/logger_error.h>
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

// Process lifecycle of the Quill backend. Quill starts its backend once per process and never
// restarts it after a stop, so these three states are genuinely distinct and a single value
// captures all of them without a second flag to keep in sync.
enum class BackendPhase : std::uint8_t
{
    NotStarted,
    Running,
    Stopped,
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

    // Single backend lifecycle source of truth. Atomic so the log macros can read liveness on the
    // caller thread without taking the sink lock.
    std::atomic<BackendPhase> phase = BackendPhase::NotStarted;

    // Runtime severity floor from Logger::Config, applied to every logger handed out. Atomic
    // because category loggers can first be requested from worker threads.
    std::atomic<quill::LogLevel> default_level = quill::LogLevel::Info;
};

[[nodiscard]] LoggingState& loggingState()
{
    static LoggingState g_logging_state;
    return g_logging_state;
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

[[nodiscard]] constexpr quill::LogLevel toQuillLevel(const Logger::Level level) noexcept
{
    switch (level)
    {
        case Logger::Level::Trace:
        {
            return quill::LogLevel::TraceL1;
        }
        case Logger::Level::Debug:
        {
            return quill::LogLevel::Debug;
        }
        case Logger::Level::Info:
        {
            return quill::LogLevel::Info;
        }
        case Logger::Level::Warning:
        {
            return quill::LogLevel::Warning;
        }
        case Logger::Level::Error:
        {
            return quill::LogLevel::Error;
        }
    }
    return quill::LogLevel::Info;
}

[[nodiscard]] std::vector<std::shared_ptr<quill::Sink>> currentSinks()
{
    LoggingState& state = loggingState();
    const std::scoped_lock lock{state.sinks_mutex};
    return state.sinks;
}

void installSinks(std::vector<std::shared_ptr<quill::Sink>> sinks)
{
    LoggingState& state = loggingState();
    const std::scoped_lock lock{state.sinks_mutex};
    state.sinks = std::move(sinks);
}

[[nodiscard]] std::expected<std::shared_ptr<quill::Sink>, LoggerError> createFileSink(
    const Logger::Config& config)
{
    const std::filesystem::path parent_directory = config.log_file.parent_path();
    if (!parent_directory.empty())
    {
        std::error_code error;
        std::filesystem::create_directories(parent_directory, error);
        if (error)
        {
            return std::unexpected{LoggerError{
                LoggerErrorCode::FileSinkOpenFailed,
                "could not create log directory: " + error.message(),
            }};
        }
    }

    try
    {
        quill::RotatingFileSinkConfig file_config;
        file_config.set_open_mode('a');
        file_config.set_rotation_max_file_size(config.max_file_size_bytes);
        file_config.set_max_backup_files(static_cast<std::uint32_t>(config.max_backup_files));
        // Quill takes the log filename only as a std::string sink name, so it is narrowed here; on
        // Windows that is the active-code-page form (see the non-ASCII log-path watch item). The
        // path itself is built losslessly at the app roots.
        return quill::Frontend::create_or_get_sink<quill::RotatingFileSink>(
            config.log_file.string(), file_config);
    }
    catch (const std::exception& exception)
    {
        return std::unexpected{LoggerError{
            LoggerErrorCode::FileSinkOpenFailed,
            exceptionText(exception),
        }};
    }
    catch (...)
    {
        return std::unexpected{LoggerError{
            LoggerErrorCode::FileSinkOpenFailed,
            "unknown error opening log file",
        }};
    }
}

} // namespace

// Opens the configured sinks, starts the async backend, and bridges JUCE output when there is
// somewhere visible to write it. Sinks open before the backend starts so a sink that cannot be
// opened returns an error with nothing started; a failed call has no side effects and may be
// retried, for example with a console-only config as a fallback.
std::expected<void, LoggerError> Logger::init(const Config& config)
{
    LoggingState& state = loggingState();
    switch (state.phase.load(std::memory_order_acquire))
    {
        case BackendPhase::Running:
        {
            // Already initialized; init is idempotent while the backend runs.
            return {};
        }
        case BackendPhase::Stopped:
        {
            return std::unexpected{LoggerError{LoggerErrorCode::AlreadyShutDown}};
        }
        case BackendPhase::NotStarted:
        {
            break;
        }
    }

    std::vector<std::shared_ptr<quill::Sink>> sinks;
    bool has_visible_sink = false;

    if (!config.log_file.empty())
    {
        auto file_sink = createFileSink(config);
        if (!file_sink.has_value())
        {
            return std::unexpected{std::move(file_sink.error())};
        }
        sinks.push_back(std::move(*file_sink));
        has_visible_sink = true;
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

    try
    {
        quill::Backend::start();
    }
    catch (const std::exception& exception)
    {
        return std::unexpected{LoggerError{
            LoggerErrorCode::BackendStartFailed,
            exceptionText(exception),
        }};
    }
    catch (...)
    {
        return std::unexpected{LoggerError{LoggerErrorCode::BackendStartFailed}};
    }

    installSinks(std::move(sinks));
    state.default_level.store(toQuillLevel(config.default_level), std::memory_order_release);
    state.phase.store(BackendPhase::Running, std::memory_order_release);

    if (has_visible_sink)
    {
        state.juce_bridge = std::make_unique<JuceQuillBridge>(get(Category{"juce"}));
        juce::Logger::setCurrentLogger(state.juce_bridge.get());
    }

    return {};
}

// Removes the JUCE bridge, then flushes and stops the backend so no record is lost at shutdown. The
// backend moves to its terminal stopped state and is never restarted in this process.
void Logger::shutdown()
{
    LoggingState& state = loggingState();
    BackendPhase running = BackendPhase::Running;
    if (!state.phase.compare_exchange_strong(
            running, BackendPhase::Stopped, std::memory_order_acq_rel))
    {
        return;
    }

    juce::Logger::setCurrentLogger(nullptr);
    state.juce_bridge.reset();

    quill::Backend::stop();

    const std::scoped_lock lock{state.sinks_mutex};
    state.sinks.clear();
}

bool Logger::isStarted() noexcept
{
    return loggingState().phase.load(std::memory_order_acquire) == BackendPhase::Running;
}

// Pre-allocates the bounded realtime frontend queue for the current thread.
void Logger::prepareRealtimeThread()
{
    RealtimeFrontend::preallocate();
}

// Creates the named normal logger on first request and returns the backend-cached handle
// thereafter. The configured severity floor is (re)applied on every request: call sites cache
// the handle behind a static, so this runs once per call site and stays cheap.
Logger::Handle Logger::get(Category category)
{
    Logger::Handle logger =
        quill::Frontend::create_or_get_logger(categoryText(category), currentSinks(), logPattern());
    logger->set_log_level(loggingState().default_level.load(std::memory_order_acquire));
    return logger;
}

// Creates the named realtime logger with a bounded-dropping frontend queue.
Logger::RealtimeHandle Logger::getRealtime(Category category)
{
    Logger::RealtimeHandle logger = RealtimeFrontend::create_or_get_logger(
        realtimeCategoryText(category), currentSinks(), logPattern());
    logger->set_log_level(loggingState().default_level.load(std::memory_order_acquire));
    return logger;
}

} // namespace rock_hero::common::core
