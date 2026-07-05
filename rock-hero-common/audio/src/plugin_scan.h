/*!
\file plugin_scan.h
\brief Plugin identity, description, and catalog-scan support for the engine plugin host.
*/

#pragma once

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <functional>
#include <juce_audio_processors/juce_audio_processors.h>
#include <optional>
#include <rock_hero/common/audio/plugin/i_plugin_host.h>
#include <string>

namespace rock_hero::common::audio
{

// Serializable plugin identity stored in the audio-owned tone document.
struct PluginIdentity
{
    std::string format_name;
    std::string name;
    std::string descriptive_name;
    std::string manufacturer;
    std::string version;
    std::string unique_id;
    std::string deprecated_uid;
    bool is_instrument{false};
    std::string original_file_or_identifier;
    std::string juce_identifier_hint;
    std::string tracktion_identifier_hint;
};
[[nodiscard]] std::chrono::milliseconds elapsedMilliseconds(
    const std::chrono::steady_clock::time_point started_at);

void logPluginCatalogScanSummary(
    const std::size_t candidate_paths, const std::chrono::milliseconds total_duration);

void reportPluginCatalogScanProgress(
    const PluginCatalogScanProgressCallback& progress_callback, std::size_t completed_plugins,
    std::size_t total_plugins, const std::filesystem::path& active_plugin_path);

void logPluginValidationSummary(
    const std::filesystem::path& plugin_path, const std::chrono::milliseconds total_duration,
    const std::optional<std::string>& failure_message);

// Cancels Tracktion's custom plugin scanner if a child-process scan never replies.
class PluginScanTimeout final
{
public:
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

    ~PluginScanTimeout()
    {
        finish();
    }

    PluginScanTimeout(const PluginScanTimeout&) = delete;
    PluginScanTimeout& operator=(const PluginScanTimeout&) = delete;
    PluginScanTimeout(PluginScanTimeout&&) = delete;
    PluginScanTimeout& operator=(PluginScanTimeout&&) = delete;

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
// Converts a plugin description into durable identity fields plus non-authoritative lookup hints.
[[nodiscard]] PluginIdentity makePluginIdentity(const juce::PluginDescription& description);

// Converts identity data back into a JUCE description shape for matching known plugins.
[[nodiscard]] juce::PluginDescription makePluginDescription(const PluginIdentity& identity);

// Chooses stable display text for progress reports before Tracktion recreates the plugin.
[[nodiscard]] std::string pluginDisplayName(
    const PluginIdentity& identity, std::size_t plugin_index);

// Builds the opaque project-owned candidate that UI and core callers can pass back to the host.
[[nodiscard]] PluginCandidate makePluginCandidate(
    const juce::PluginDescription& description, const std::filesystem::path& plugin_path);

// Reads the identity object for one tone plugin record.
[[nodiscard]] PluginIdentity readPluginIdentity(const juce::var& object);

} // namespace rock_hero::common::audio
