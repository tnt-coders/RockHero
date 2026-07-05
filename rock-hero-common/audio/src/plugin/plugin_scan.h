/*!
\file plugin_scan.h
\brief Plugin identity, description, and catalog-scan support for the engine plugin host.
*/

#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <filesystem>
#include <functional>
#include <juce_audio_processors/juce_audio_processors.h>
#include <mutex>
#include <optional>
#include <rock_hero/common/audio/plugin/i_plugin_host.h>
#include <string>
#include <thread>
#include <utility>

namespace rock_hero::common::audio
{

/*! \brief Serializable plugin identity stored in the audio-owned tone document. */
struct PluginIdentity
{
    /*! \brief Plugin format name, currently always VST3. */
    std::string format_name;

    /*! \brief Plugin display name. */
    std::string name;

    /*! \brief Longer descriptive plugin name, when the plugin provides one. */
    std::string descriptive_name;

    /*! \brief Plugin manufacturer name. */
    std::string manufacturer;

    /*! \brief Plugin version text. */
    std::string version;

    /*! \brief Durable unique plugin ID in hex text form. */
    std::string unique_id;

    /*! \brief Legacy JUCE plugin UID kept for matching older persisted state. */
    std::string deprecated_uid;

    /*! \brief True when the plugin reports itself as an instrument. */
    bool is_instrument{false};

    /*! \brief Plugin file path or identifier as originally persisted. */
    std::string original_file_or_identifier;

    /*! \brief Non-authoritative JUCE identifier for fast known-plugin lookup. */
    std::string juce_identifier_hint;

    /*! \brief Non-authoritative Tracktion identifier for fast known-plugin lookup. */
    std::string tracktion_identifier_hint;
};

/*!
\brief Measures elapsed wall time since a steady-clock start point.
\param started_at Start of the measured interval.
\return Elapsed milliseconds.
*/
[[nodiscard]] std::chrono::milliseconds elapsedMilliseconds(
    const std::chrono::steady_clock::time_point started_at);

/*!
\brief Logs one catalog-scan summary line with path count and duration.
\param candidate_paths Number of plugin paths the scan visited.
\param total_duration Total scan duration.
*/
void logPluginCatalogScanSummary(
    const std::size_t candidate_paths, const std::chrono::milliseconds total_duration);

/*!
\brief Sends scan progress to the caller-provided callback when one exists.
\param progress_callback Optional progress callback supplied by the caller.
\param completed_plugins Plugins already validated.
\param total_plugins Total plugins the scan will visit.
\param active_plugin_path Path of the plugin about to be validated.
*/
void reportPluginCatalogScanProgress(
    const PluginCatalogScanProgressCallback& progress_callback, std::size_t completed_plugins,
    std::size_t total_plugins, const std::filesystem::path& active_plugin_path);

/*!
\brief Logs one per-plugin validation summary line.
\param plugin_path Path of the validated plugin.
\param total_duration Validation duration.
\param failure_message Failure detail, or empty when validation succeeded.
*/
void logPluginValidationSummary(
    const std::filesystem::path& plugin_path, const std::chrono::milliseconds total_duration,
    const std::optional<std::string>& failure_message);

/*!
\brief Cancels Tracktion's custom plugin scanner if a child-process scan never replies.

The watchdog thread aborts the scan through the supplied callback once the timeout elapses;
finish() must be called (or the object destroyed) after the scan returns so the thread joins.
*/
class PluginScanTimeout final
{
public:
    /*!
    \brief Starts the watchdog thread for one scan call.
    \param abort_scan Callback that aborts the in-flight Tracktion scan.
    \param timeout Time allowed before the scan is aborted.
    */
    PluginScanTimeout(std::function<void()> abort_scan, std::chrono::milliseconds timeout)
        : m_abort_scan(std::move(abort_scan))
        , m_thread([this, timeout] {
            std::unique_lock lock{m_mutex};
            if (m_finished_condition.wait_for(lock, timeout, [this] { return m_finished; }))
            {
                return;
            }

            lock.unlock();
            m_timed_out.store(true);
            if (m_abort_scan)
            {
                m_abort_scan();
            }
        })
    {}

    /*! \brief Stops the watchdog and joins its thread. */
    ~PluginScanTimeout()
    {
        finish();
    }

    /*! \brief Copying is disabled; the watchdog owns a joinable thread. */
    PluginScanTimeout(const PluginScanTimeout&) = delete;

    /*! \brief Copy assignment is disabled; the watchdog owns a joinable thread. */
    PluginScanTimeout& operator=(const PluginScanTimeout&) = delete;

    /*! \brief Moving is disabled; the watchdog thread captures this object. */
    PluginScanTimeout(PluginScanTimeout&&) = delete;

    /*! \brief Move assignment is disabled; the watchdog thread captures this. */
    PluginScanTimeout& operator=(PluginScanTimeout&&) = delete;

    /*! \brief Marks the scan finished and joins the watchdog thread. */
    void finish()
    {
        {
            const std::scoped_lock lock{m_mutex};
            m_finished = true;
        }

        m_finished_condition.notify_one();

        if (m_thread.joinable())
        {
            m_thread.join();
        }
    }

    /*!
    \brief Reports whether the watchdog aborted the scan.
    \return True when the scan timed out and was aborted.
    */
    [[nodiscard]] bool timedOut() const noexcept
    {
        return m_timed_out.load();
    }

private:
    std::function<void()> m_abort_scan;
    std::mutex m_mutex;
    std::condition_variable m_finished_condition;
    bool m_finished{false};
    std::atomic_bool m_timed_out{false};
    std::thread m_thread;
};
/*!
\brief Converts a plugin description into durable identity fields plus lookup hints.
\param description JUCE plugin description to persist.
\return Serializable identity for the tone document.
*/
[[nodiscard]] PluginIdentity makePluginIdentity(const juce::PluginDescription& description);

/*!
\brief Converts identity data back into a JUCE description for known-plugin matching.
\param identity Persisted plugin identity.
\return JUCE description shape for duplicate matching.
*/
[[nodiscard]] juce::PluginDescription makePluginDescription(const PluginIdentity& identity);

/*!
\brief Chooses stable display text for progress reports before the plugin exists.
\param identity Persisted plugin identity.
\param plugin_index Zero-based chain position used for the placeholder name.
\return Display name for progress messages.
*/
[[nodiscard]] std::string pluginDisplayName(
    const PluginIdentity& identity, std::size_t plugin_index);

/*!
\brief Builds the opaque project-owned candidate callers pass back to the host.
\param description JUCE plugin description found by a scan.
\param plugin_path Normalized plugin path for display and deduplication.
\return Project-owned plugin candidate.
*/
[[nodiscard]] PluginCandidate makePluginCandidate(
    const juce::PluginDescription& description, const std::filesystem::path& plugin_path);

/*!
\brief Reads the identity object for one tone plugin record.
\param object Parsed identity JSON object.
\return Persisted plugin identity fields.
*/
[[nodiscard]] PluginIdentity readPluginIdentity(const juce::var& object);

} // namespace rock_hero::common::audio
