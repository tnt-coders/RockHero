/*!
\file logger.h
\brief Project-owned logging facade backed by Quill.
*/

#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <rock_hero/common/core/shared/logger_error.h>
#include <string>
#include <string_view>

#ifndef QUILL_DISABLE_NON_PREFIXED_MACROS
/*! \brief Keeps Quill's global unprefixed logging macros out of project call sites. */
#define QUILL_DISABLE_NON_PREFIXED_MACROS 1
#endif

#include <quill/LogMacros.h>
#include <quill/Logger.h>

namespace rock_hero::common::core
{

/*!
\brief Non-instantiable logging facade grouping backend setup and category helpers.

Normal project code logs through `RH_LOG_*("category", "message {}", value)`. Realtime-capable
code uses the explicit `RH_RT_LOG_*` macros with a pre-created \ref RealtimeHandle so the audio
thread never performs logger lookup or category creation.

Field convention: format string values with `{}` for scalars and `{:?}` for strings. The `{:?}`
debug specifier quotes and escapes the value, so whitespace and delimiters in names, paths, and
messages stay unambiguous in `key=value` records (`label_hint={:?}` renders `label_hint="My
Plugin"`). The specifier is resolved on the backend thread, so it behaves identically in the
normal and `RH_RT_LOG_*` realtime macros and costs nothing extra on the calling thread.
*/
struct Logger
{
    /*! \brief Prevents construction; all behavior is exposed as static helpers and nested types. */
    Logger() = delete;

    /*! \brief Opaque handle to a normal backend logger. */
    using Handle = quill::Logger*;

    /*!
    \brief Frontend options for realtime-capable loggers.

    The bounded dropping queue is the important distinction from Quill's default frontend. A full
    realtime queue drops the attempted record instead of allocating or blocking the caller. Callers
    must still keep realtime arguments cheap and must prepare the audio thread before first use.
    */
    struct RealtimeFrontendOptions
    {
        /*! \brief Fixed-capacity queue that drops when full rather than blocking. */
        static constexpr quill::QueueType queue_type = quill::QueueType::BoundedDropping;

        /*! \brief Per-thread realtime queue size allocated by \ref prepareRealtimeThread. */
        static constexpr std::size_t initial_queue_capacity = std::size_t{256} * std::size_t{1024};

        /*! \brief Unused for dropping queues, kept zero to document the no-blocking contract. */
        static constexpr std::uint32_t blocking_queue_retry_interval_ns = 0;

        /*! \brief Unused for bounded queues, kept equal to the fixed capacity. */
        static constexpr std::size_t unbounded_queue_max_capacity = initial_queue_capacity;

        /*! \brief Avoid platform-specific huge-page setup on realtime threads. */
        static constexpr quill::HugePagesPolicy huge_pages_policy = quill::HugePagesPolicy::Never;
    };

    /*! \brief Opaque handle to a bounded-dropping realtime backend logger. */
    using RealtimeHandle = quill::LoggerImpl<RealtimeFrontendOptions>*;

    /*!
    \brief Compile-time-validated subsystem tag used to name a logger.

    Categories follow the `product.subsystem[.detail]` convention: lowercase segments of
    `[a-z0-9_-]` separated by single dots, with no empty leading, trailing, or interior segment.
    */
    class Category
    {
    public:
        /*!
        \brief Creates a category from a compile-time category literal.
        \param name Dotted category text; ill-formed text is a compile error.
        */
        explicit consteval Category(std::string_view name)
            : m_name{name}
        {
            if (!isWellFormed(name))
            {
                throw "log category must be lowercase dotted segments of [a-z0-9_-]";
            }
        }

        /*!
        \brief Returns the validated category text.
        \return Dotted category name.
        */
        [[nodiscard]] constexpr std::string_view name() const noexcept
        {
            return m_name;
        }

    private:
        // Enforces the dotted-lowercase shape so every category stays a clean, filterable name.
        [[nodiscard]] static constexpr bool isWellFormed(std::string_view name) noexcept
        {
            if (name.empty())
            {
                return false;
            }

            bool segment_has_char = false;
            for (const char character : name)
            {
                if (character == '.')
                {
                    if (!segment_has_char)
                    {
                        return false;
                    }
                    segment_has_char = false;
                    continue;
                }

                const bool allowed = (character >= 'a' && character <= 'z') ||
                                     (character >= '0' && character <= '9') || character == '_' ||
                                     character == '-';
                if (!allowed)
                {
                    return false;
                }
                segment_has_char = true;
            }

            return segment_has_char;
        }

        std::string_view m_name;
    };

    /*! \brief File-sink and console configuration for the process logging backend. */
    struct Config
    {
        /*! \brief Rotating log-file target. Parent directories are created if needed. */
        std::filesystem::path log_file;

        /*! \brief Size at which the active log file rotates to a numbered backup. */
        std::size_t max_file_size_bytes = std::size_t{8} * std::size_t{1024} * std::size_t{1024};

        /*! \brief Number of rotated backup files retained before the oldest is dropped. */
        std::size_t max_backup_files = 5;

        /*! \brief When true, also mirror records to the console. */
        bool log_to_console = false;
    };

    /*!
    \brief Starts the Quill backend and installs configured sinks.

    Sinks are opened before the backend starts, so a configured sink that cannot be opened fails the
    call with nothing started: a missing log file reports \ref LoggerErrorCode::FileSinkOpenFailed
    and leaves the backend startable. A failed call has no side effects and may be retried, so a
    caller that wants console fallback can re-invoke init with a console-only \ref Config. When no
    visible sink is configured, a null sink is installed and the call succeeds. Log-file parent
    directories are created as needed.

    Call once from the application composition root before other services can log. Quill's backend
    is process-lifetime infrastructure: calling init again while running succeeds without restarting
    it, and calling init after shutdown fails with \ref LoggerErrorCode::AlreadyShutDown rather than
    pretending the backend can restart.

    \param config Sink and file configuration.
    \return Nothing on success, or a \ref LoggerError describing why startup failed.
    */
    [[nodiscard]] static std::expected<void, LoggerError> init(const Config& config);

    /*!
    \brief Flushes and stops the logging backend and removes the JUCE bridge.

    Call once during application shutdown after the last record is emitted.
    */
    static void shutdown();

    /*!
    \brief Reports whether the backend worker is accepting project log records.

    The logging macros use this as a cheap lifetime guard so log calls after startup failure or
    during shutdown do not enqueue records into a stopped backend.
    \return True when project log calls may enqueue records.
    */
    [[nodiscard]] static bool isStarted() noexcept;

    /*!
    \brief Pre-allocates realtime queue storage for the current thread.

    Call this from the target thread before it enters a realtime callback. This only prepares the
    thread-local queue; realtime logger handles must also be created off the realtime path.
    */
    static void prepareRealtimeThread();

    /*!
    \brief Returns the handle for a category, creating its normal backend logger on first use.

    Requires a successful \ref init call. Normal call sites should prefer `RH_LOG_*`, which checks
    backend lifetime before requesting the category handle.

    \param category Validated subsystem tag used as the logger name.
    \return Backend-owned handle for the category.
    */
    [[nodiscard]] static Handle get(Category category);

    /*!
    \brief Returns a bounded-dropping realtime logger for a pre-created category.

    Requires a successful \ref init call. Create and store this handle from non-realtime setup code.
    Passing the handle to `RH_RT_LOG_*` avoids category lookup, string construction, and locking on
    the audio thread.

    \param category Validated subsystem tag used as the logger name.
    \return Backend-owned realtime handle for the category.
    */
    [[nodiscard]] static RealtimeHandle getRealtime(Category category);
};

} // namespace rock_hero::common::core

/*! \brief Forces a preprocessor re-scan of a forwarded macro call. */
/* MSVC's traditional preprocessor (the project default; /Zc:preprocessor is not enabled) passes
a forwarded __VA_ARGS__ pack into the inner macro as one glued argument; the extra expansion
re-tokenizes the call so the arguments split correctly on every compiler. */
#define RH_DETAIL_EXPAND(call) call

/*! \brief Internal helper used by normal project logging macros. */
/* The format string travels inside __VA_ARGS__ (callers always pass one), so the pack is never
empty and no comma-elision extension is needed: `##__VA_ARGS__` is a GNU extension that pedantic
clang flags, and MSVC's traditional preprocessor rejects the standard __VA_OPT__ alternative. */
#define RH_DETAIL_LOG(log_macro, category_literal, ...)                                            \
    do                                                                                             \
    {                                                                                              \
        if (::rock_hero::common::core::Logger::isStarted())                                        \
        {                                                                                          \
            static const auto rock_hero_log_handle_internal__ =                                    \
                ::rock_hero::common::core::Logger::get(                                            \
                    ::rock_hero::common::core::Logger::Category{category_literal});                \
            RH_DETAIL_EXPAND(log_macro(rock_hero_log_handle_internal__, __VA_ARGS__));             \
        }                                                                                          \
    } while (false)

/*! \brief Internal helper used by realtime project logging macros. */
#define RH_DETAIL_RT_LOG(log_macro, realtime_logger, ...)                                          \
    do                                                                                             \
    {                                                                                              \
        if (::rock_hero::common::core::Logger::isStarted())                                        \
        {                                                                                          \
            auto* const rock_hero_rt_log_handle_internal__ = (realtime_logger);                    \
            if (rock_hero_rt_log_handle_internal__ != nullptr)                                     \
            {                                                                                      \
                RH_DETAIL_EXPAND(log_macro(rock_hero_rt_log_handle_internal__, __VA_ARGS__));      \
            }                                                                                      \
        }                                                                                          \
    } while (false)

#if QUILL_COMPILE_ACTIVE_LOG_LEVEL <= QUILL_COMPILE_ACTIVE_LOG_LEVEL_TRACE_L1
/*! \brief Logs a trace-level record to the named category when trace logging is compiled in. */
#define RH_LOG_TRACE(category_literal, ...)                                                        \
    RH_DETAIL_LOG(QUILL_LOG_TRACE_L1, category_literal, __VA_ARGS__)
#else
/*! \brief Discards trace-level records when trace logging is compiled out. */
#define RH_LOG_TRACE(category_literal, ...) (void)0
#endif

#if QUILL_COMPILE_ACTIVE_LOG_LEVEL <= QUILL_COMPILE_ACTIVE_LOG_LEVEL_DEBUG
/*! \brief Logs a debug-level record to the named category when debug logging is compiled in. */
#define RH_LOG_DEBUG(category_literal, ...)                                                        \
    RH_DETAIL_LOG(QUILL_LOG_DEBUG, category_literal, __VA_ARGS__)
#else
/*! \brief Discards debug-level records when debug logging is compiled out. */
#define RH_LOG_DEBUG(category_literal, ...) (void)0
#endif

#if QUILL_COMPILE_ACTIVE_LOG_LEVEL <= QUILL_COMPILE_ACTIVE_LOG_LEVEL_INFO
/*! \brief Logs an info-level record to the named category when info logging is compiled in. */
#define RH_LOG_INFO(category_literal, ...)                                                         \
    RH_DETAIL_LOG(QUILL_LOG_INFO, category_literal, __VA_ARGS__)
#else
/*! \brief Discards info-level records when info logging is compiled out. */
#define RH_LOG_INFO(category_literal, ...) (void)0
#endif

#if QUILL_COMPILE_ACTIVE_LOG_LEVEL <= QUILL_COMPILE_ACTIVE_LOG_LEVEL_WARNING
/*! \brief Logs a warning-level record to the named category when warning logging is compiled in. */
#define RH_LOG_WARNING(category_literal, ...)                                                      \
    RH_DETAIL_LOG(QUILL_LOG_WARNING, category_literal, __VA_ARGS__)
#else
/*! \brief Discards warning-level records when warning logging is compiled out. */
#define RH_LOG_WARNING(category_literal, ...) (void)0
#endif

#if QUILL_COMPILE_ACTIVE_LOG_LEVEL <= QUILL_COMPILE_ACTIVE_LOG_LEVEL_ERROR
/*! \brief Logs an error-level record to the named category when error logging is compiled in. */
#define RH_LOG_ERROR(category_literal, ...)                                                        \
    RH_DETAIL_LOG(QUILL_LOG_ERROR, category_literal, __VA_ARGS__)
#else
/*! \brief Discards error-level records when error logging is compiled out. */
#define RH_LOG_ERROR(category_literal, ...) (void)0
#endif

#if QUILL_COMPILE_ACTIVE_LOG_LEVEL <= QUILL_COMPILE_ACTIVE_LOG_LEVEL_CRITICAL
/*! \brief Logs a critical-level record when critical logging is compiled in. */
#define RH_LOG_CRITICAL(category_literal, ...)                                                     \
    RH_DETAIL_LOG(QUILL_LOG_CRITICAL, category_literal, __VA_ARGS__)
#else
/*! \brief Discards critical-level records when critical logging is compiled out. */
#define RH_LOG_CRITICAL(category_literal, ...) (void)0
#endif

/*! \brief Logs a trace-level record through a pre-created realtime logger handle. */
#define RH_RT_LOG_TRACE(logger, ...) RH_DETAIL_RT_LOG(QUILL_LOG_TRACE_L1, logger, __VA_ARGS__)
/*! \brief Logs a debug-level record through a pre-created realtime logger handle. */
#define RH_RT_LOG_DEBUG(logger, ...) RH_DETAIL_RT_LOG(QUILL_LOG_DEBUG, logger, __VA_ARGS__)
/*! \brief Logs an info-level record through a pre-created realtime logger handle. */
#define RH_RT_LOG_INFO(logger, ...) RH_DETAIL_RT_LOG(QUILL_LOG_INFO, logger, __VA_ARGS__)
/*! \brief Logs a warning-level record through a pre-created realtime logger handle. */
#define RH_RT_LOG_WARNING(logger, ...) RH_DETAIL_RT_LOG(QUILL_LOG_WARNING, logger, __VA_ARGS__)
/*! \brief Logs an error-level record through a pre-created realtime logger handle. */
#define RH_RT_LOG_ERROR(logger, ...) RH_DETAIL_RT_LOG(QUILL_LOG_ERROR, logger, __VA_ARGS__)
/*! \brief Logs a critical-level record through a pre-created realtime logger handle. */
#define RH_RT_LOG_CRITICAL(logger, ...) RH_DETAIL_RT_LOG(QUILL_LOG_CRITICAL, logger, __VA_ARGS__)
