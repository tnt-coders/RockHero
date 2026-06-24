/*!
\file recording_plugin_host.h
\brief Recording plugin-host test implementation.
*/

#pragma once

#include <algorithm>
#include <charconv>
#include <cstddef>
#include <expected>
#include <filesystem>
#include <iterator>
#include <optional>
#include <rock_hero/common/audio/i_plugin_host.h>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_map>
#include <utility>
#include <vector>

namespace rock_hero::common::audio::testing
{

/*!
\brief IPluginHost implementation that records catalog, chain, and window operations.

Use this when tests need plugin-host behavior without scanning plugin folders or loading real
plugins. The fake keeps catalog data configurable while preserving the production port's typed
success and error channels.
*/
class RecordingPluginHost final : public IPluginHost
{
public:
    /*!
    \brief Records a default catalog scan and refreshes the known catalog on success.
    \param progress_callback Optional callback receiving configured scan progress.
    \param cancel Cancellation token observed before each configured progress payload.
    \return Empty success or the next configured catalog-scan error.
    */
    [[nodiscard]] std::expected<void, PluginHostError> scanPluginCatalog(
        PluginCatalogScanProgressCallback progress_callback = {},
        const common::core::CancellationToken& cancel = {}) override
    {
        catalog_scan_call_count += 1;
        if (next_catalog_scan_error.has_value())
        {
            return std::unexpected{*next_catalog_scan_error};
        }

        for (const PluginCatalogScanProgress& progress : next_catalog_scan_progress)
        {
            // Mirror the production per-candidate cancellation check so tests can assert the scan
            // stops early and leaves the previously known catalog untouched.
            if (cancel.isCancelled())
            {
                catalog_scan_canceled = true;
                return {};
            }

            if (progress_callback)
            {
                progress_callback(progress);
            }
        }

        next_known_candidates = next_catalog_candidates;
        return {};
    }

    /*!
    \brief Records explicit scan roots and returns the configured catalog candidates.
    \param roots Plugin files or directories requested by the object under test.
    \param progress_callback Optional callback receiving configured scan progress.
    \return Configured catalog candidates or the next configured catalog-scan error.
    */
    [[nodiscard]] std::expected<std::vector<PluginCandidate>, PluginHostError> scanPluginLocations(
        const std::vector<std::filesystem::path>& roots,
        PluginCatalogScanProgressCallback progress_callback = {}) override
    {
        last_scan_roots = roots;
        catalog_scan_call_count += 1;
        if (next_catalog_scan_error.has_value())
        {
            return std::unexpected{*next_catalog_scan_error};
        }

        for (const PluginCatalogScanProgress& progress : next_catalog_scan_progress)
        {
            if (progress_callback)
            {
                progress_callback(progress);
            }
        }

        return next_catalog_candidates;
    }

    /*!
    \brief Returns the configured known catalog without simulating an expensive scan.
    \return Configured known plugin catalog.
    */
    [[nodiscard]] std::vector<PluginCandidate> knownPluginCatalog() const override
    {
        known_candidates_call_count += 1;
        return next_known_candidates;
    }

    /*!
    \brief Records a plugin insertion request and mutates the in-memory chain.
    \param plugin_candidate Candidate requested for insertion.
    \param chain_index User-visible insertion index.
    \return Updated chain snapshot plus inserted runtime ID, or a configured/validation failure.
    */
    [[nodiscard]] std::expected<PluginInsertResult, PluginHostError> insertPlugin(
        const PluginCandidate& plugin_candidate, std::size_t chain_index) override
    {
        last_inserted_plugin_candidate = plugin_candidate;
        last_insert_index = chain_index;
        insert_call_count += 1;
        if (next_insert_error.has_value())
        {
            return std::unexpected{*next_insert_error};
        }

        if (chain_index > chain.size())
        {
            return std::unexpected{PluginHostError{PluginHostErrorCode::InvalidChainIndex}};
        }

        if (chain.size() >= max_signal_chain_plugins)
        {
            return std::unexpected{PluginHostError{
                PluginHostErrorCode::PluginChainLimitExceeded,
                pluginChainLimitExceededMessage(chain.size()),
            }};
        }

        const std::vector<PluginChainEntry> previous_chain = chain;
        const std::unordered_map<std::string, PluginInstanceState> previous_states =
            m_instance_states;
        PluginChainEntry entry{
            .instance_id = next_instance_id,
            .plugin_id = plugin_candidate.id,
            .name = plugin_candidate.name,
            .manufacturer = plugin_candidate.manufacturer,
            .format_name = plugin_candidate.format_name,
            .category = plugin_candidate.category,
            .chain_index = chain_index,
        };
        const std::string inserted_instance_id = entry.instance_id;
        chain.insert(chain.begin() + static_cast<std::ptrdiff_t>(chain_index), std::move(entry));
        reindexChain();
        m_instance_states[inserted_instance_id] = makePluginInstanceState(chain[chain_index]);

        if (next_insert_after_mutation_error.has_value())
        {
            chain = previous_chain;
            m_instance_states = previous_states;
            return std::unexpected{*next_insert_after_mutation_error};
        }

        return PluginInsertResult{
            .snapshot = snapshot(),
            .inserted_instance_id = inserted_instance_id,
        };
    }

    /*!
    \brief Records a plugin move request and mutates the in-memory chain.
    \param instance_id Runtime plugin instance requested for movement.
    \param destination_index Destination user-visible chain index.
    \return Updated chain snapshot, or a configured/validation failure.
    */
    [[nodiscard]] std::expected<PluginChainSnapshot, PluginHostError> movePlugin(
        const std::string& instance_id, std::size_t destination_index) override
    {
        last_moved_instance_id = instance_id;
        last_move_destination_index = destination_index;
        move_call_count += 1;
        if (next_move_error.has_value())
        {
            return std::unexpected{*next_move_error};
        }

        const auto plugin = findPlugin(instance_id);
        if (plugin == chain.end())
        {
            return std::unexpected{PluginHostError{
                PluginHostErrorCode::PluginInstanceNotFound,
                "Plugin instance was not found: " + instance_id,
            }};
        }

        if (destination_index >= chain.size())
        {
            return std::unexpected{PluginHostError{PluginHostErrorCode::InvalidChainIndex}};
        }

        const auto current_index = static_cast<std::size_t>(std::distance(chain.begin(), plugin));
        if (current_index == destination_index)
        {
            return snapshot();
        }

        const std::vector<PluginChainEntry> previous_chain = chain;
        const std::unordered_map<std::string, PluginInstanceState> previous_states =
            m_instance_states;
        PluginChainEntry entry = std::move(*plugin);
        chain.erase(plugin);
        chain.insert(
            chain.begin() + static_cast<std::ptrdiff_t>(destination_index), std::move(entry));
        reindexChain();
        if (next_move_after_mutation_error.has_value())
        {
            chain = previous_chain;
            m_instance_states = previous_states;
            return std::unexpected{*next_move_after_mutation_error};
        }

        return snapshot();
    }

    /*!
    \brief Records a plugin removal request and mutates the in-memory chain.
    \param instance_id Runtime plugin instance requested for removal.
    \return Updated chain snapshot, or a configured/validation failure.
    */
    [[nodiscard]] std::expected<PluginChainSnapshot, PluginHostError> removePlugin(
        const std::string& instance_id) override
    {
        last_removed_instance_id = instance_id;
        remove_call_count += 1;
        if (next_remove_error.has_value())
        {
            return std::unexpected{*next_remove_error};
        }

        const auto plugin = findPlugin(instance_id);
        if (plugin == chain.end())
        {
            return std::unexpected{PluginHostError{
                PluginHostErrorCode::PluginInstanceNotFound,
                "Plugin instance was not found: " + instance_id,
            }};
        }

        const std::vector<PluginChainEntry> previous_chain = chain;
        const std::unordered_map<std::string, PluginInstanceState> previous_states =
            m_instance_states;
        const std::string removed_instance_id = plugin->instance_id;
        chain.erase(plugin);
        m_instance_states.erase(removed_instance_id);
        reindexChain();
        if (next_remove_after_mutation_error.has_value())
        {
            chain = previous_chain;
            m_instance_states = previous_states;
            return std::unexpected{*next_remove_after_mutation_error};
        }

        return snapshot();
    }

    /*!
    \brief Captures the fake plugin's self-contained opaque state.
    \param instance_id Runtime plugin instance requested for capture.
    \return Captured plugin state, or a configured/validation failure.
    */
    [[nodiscard]] std::expected<PluginInstanceState, PluginHostError> capturePluginState(
        const std::string& instance_id) override
    {
        last_captured_instance_id = instance_id;
        capture_state_call_count += 1;
        if (next_capture_state_error.has_value())
        {
            return std::unexpected{*next_capture_state_error};
        }

        const auto plugin = findPlugin(instance_id);
        if (plugin == chain.end())
        {
            return std::unexpected{PluginHostError{
                PluginHostErrorCode::PluginInstanceNotFound,
                "Plugin instance was not found: " + instance_id,
            }};
        }

        const auto stored_state = m_instance_states.find(instance_id);
        if (stored_state != m_instance_states.end())
        {
            return stored_state->second;
        }

        return makePluginInstanceState(*plugin);
    }

    /*!
    \brief Recreates a fake plugin state under its encoded runtime instance ID.
    \param state Opaque state previously captured from this fake.
    \param chain_index User-visible insertion index.
    \return Updated chain snapshot, or a failure.
    */
    [[nodiscard]] std::expected<PluginChainSnapshot, PluginHostError>
    recreatePluginStatePreservingId(
        const PluginInstanceState& state, std::size_t chain_index) override
    {
        last_recreated_state = state;
        last_recreate_state_index = chain_index;
        recreate_state_call_count += 1;
        if (next_recreate_state_error.has_value())
        {
            return std::unexpected{*next_recreate_state_error};
        }

        auto decoded_entry = pluginChainEntryFromState(state);
        if (!decoded_entry.has_value())
        {
            return std::unexpected{PluginHostError{
                PluginHostErrorCode::PluginStateRestoreFailed,
                "Plugin state bytes are not a RecordingPluginHost state",
            }};
        }

        if (chain_index > chain.size())
        {
            return std::unexpected{PluginHostError{PluginHostErrorCode::InvalidChainIndex}};
        }

        if (decoded_entry->instance_id.empty() ||
            findPlugin(decoded_entry->instance_id) != chain.end())
        {
            return std::unexpected{PluginHostError{
                PluginHostErrorCode::PluginStateRestoreFailed,
                "Plugin state instance id is already loaded or empty",
            }};
        }

        if (chain.size() >= max_signal_chain_plugins)
        {
            return std::unexpected{PluginHostError{
                PluginHostErrorCode::PluginChainLimitExceeded,
                pluginChainLimitExceededMessage(chain.size()),
            }};
        }

        const std::vector<PluginChainEntry> previous_chain = chain;
        const std::unordered_map<std::string, PluginInstanceState> previous_states =
            m_instance_states;
        decoded_entry->chain_index = chain_index;
        const std::string recreated_instance_id = decoded_entry->instance_id;
        chain.insert(
            chain.begin() + static_cast<std::ptrdiff_t>(chain_index), std::move(*decoded_entry));
        reindexChain();
        m_instance_states[recreated_instance_id] = makePluginInstanceState(chain[chain_index]);

        if (next_recreate_state_after_mutation_error.has_value())
        {
            chain = previous_chain;
            m_instance_states = previous_states;
            return std::unexpected{*next_recreate_state_after_mutation_error};
        }

        return snapshot();
    }

    /*!
    \brief Restores a fake plugin state onto an existing chain instance.
    \param instance_id Runtime plugin instance requested for restore.
    \param state Opaque state previously captured from this fake.
    \return Empty success, or a configured/validation failure.
    */
    [[nodiscard]] std::expected<void, PluginHostError> setPluginState(
        const std::string& instance_id, const PluginInstanceState& state) override
    {
        last_set_state_instance_id = instance_id;
        last_set_state = state;
        set_state_call_count += 1;
        if (next_set_state_error.has_value())
        {
            return std::unexpected{*next_set_state_error};
        }

        const auto plugin = findPlugin(instance_id);
        if (plugin == chain.end())
        {
            return std::unexpected{PluginHostError{
                PluginHostErrorCode::PluginInstanceNotFound,
                "Plugin instance was not found: " + instance_id,
            }};
        }

        auto decoded_entry = pluginChainEntryFromState(state);
        if (!decoded_entry.has_value())
        {
            return std::unexpected{PluginHostError{
                PluginHostErrorCode::PluginStateRestoreFailed,
                "Plugin state bytes are not a RecordingPluginHost state",
            }};
        }

        const std::vector<PluginChainEntry> previous_chain = chain;
        const std::unordered_map<std::string, PluginInstanceState> previous_states =
            m_instance_states;
        decoded_entry->instance_id = instance_id;
        decoded_entry->chain_index = plugin->chain_index;
        *plugin = std::move(*decoded_entry);
        reindexChain();
        m_instance_states[instance_id] = makePluginInstanceState(*plugin);

        if (next_set_state_after_mutation_error.has_value())
        {
            chain = previous_chain;
            m_instance_states = previous_states;
            return std::unexpected{*next_set_state_after_mutation_error};
        }

        return {};
    }

    /*! \brief Flushes the queued fake plugin edit through the installed observer. */
    void flushPendingPluginEdits() override
    {
        flush_pending_plugin_edits_call_count += 1;
        if (pending_state_edit.has_value())
        {
            PluginStateEdit edit = std::move(*pending_state_edit);
            pending_state_edit.reset();
            if (m_state_edit_observer.edit_completed)
            {
                m_state_edit_observer.edit_completed(std::move(edit));
            }
        }

        if (!pending_state_edit.has_value() && m_plugin_edit_observer.pending_changed)
        {
            m_plugin_edit_observer.pending_changed(false);
        }
    }

    /*!
    \brief Reports whether a fake plugin edit is queued.
    \return True when flushPendingPluginEdits() has an edit to emit.
    */
    [[nodiscard]] bool hasPendingPluginEdits() const override
    {
        return pending_state_edit.has_value();
    }

    /*!
    \brief Installs fake plugin-edit observer callbacks.
    \param observer Callback set replacing any previous observer.
    */
    void setPluginEditObserver(PluginEditObserver observer) override
    {
        m_plugin_edit_observer = std::move(observer);
    }

    /*!
    \brief Installs fake plugin-state-edit observer callbacks.
    \param observer Callback set replacing any previous observer.
    */
    void setPluginStateEditObserver(PluginStateEditObserver observer) override
    {
        m_state_edit_observer = std::move(observer);
    }

    /*!
    \brief Installs fake plugin-window command observer callbacks.
    \param observer Callback set replacing any previous observer.
    */
    void setPluginWindowCommandObserver(PluginWindowCommandObserver observer) override
    {
        m_window_command_observer = std::move(observer);
    }

    /*!
    \brief Queues a completed fake plugin-state edit for the next flush.
    \param edit Before/after full-state edit to emit.
    */
    void queuePendingPluginStateEdit(PluginStateEdit edit)
    {
        const bool was_pending = hasPendingPluginEdits();
        pending_state_edit = std::move(edit);
        if (!was_pending && m_plugin_edit_observer.pending_changed)
        {
            m_plugin_edit_observer.pending_changed(true);
        }
    }

    /*! \brief Emits a fake Undo shortcut from a hosted plugin editor window. */
    void notifyPluginWindowUndoRequested()
    {
        if (m_window_command_observer.undo_requested)
        {
            m_window_command_observer.undo_requested();
        }
    }

    /*! \brief Emits a fake Redo shortcut from a hosted plugin editor window. */
    void notifyPluginWindowRedoRequested()
    {
        if (m_window_command_observer.redo_requested)
        {
            m_window_command_observer.redo_requested();
        }
    }

    /*! \brief Emits a fake Play/Pause shortcut from a hosted plugin editor window. */
    void notifyPluginWindowPlayPauseRequested()
    {
        if (m_window_command_observer.play_pause_requested)
        {
            m_window_command_observer.play_pause_requested();
        }
    }

    /*!
    \brief Records a plugin editor-window request and returns the configured outcome.
    \param instance_id Runtime plugin instance whose editor was requested.
    \return Empty success or the next configured open-window error.
    */
    [[nodiscard]] std::expected<void, PluginHostError> openPluginWindow(
        const std::string& instance_id) override
    {
        last_opened_instance_id = instance_id;
        open_call_count += 1;
        if (next_open_error.has_value())
        {
            return std::unexpected{*next_open_error};
        }

        return {};
    }

    /*! \brief Candidates returned by the next successful catalog scan. */
    std::vector<PluginCandidate> next_catalog_candidates{
        PluginCandidate{
            .id = "catalog-plugin-id",
            .name = "Catalog Amp",
            .manufacturer = "Example Audio",
            .format_name = "VST3",
            .category = "Fx|Distortion",
            .file_path = std::filesystem::path{"catalog-amp.vst3"},
        },
    };

    /*! \brief Candidates returned by the lightweight known-catalog read. */
    std::vector<PluginCandidate> next_known_candidates{
        PluginCandidate{
            .id = "catalog-plugin-id",
            .name = "Catalog Amp",
            .manufacturer = "Example Audio",
            .format_name = "VST3",
            .category = "Fx|Distortion",
            .file_path = std::filesystem::path{"catalog-amp.vst3"},
        },
    };

    /*! \brief Current ordered in-memory plugin chain. */
    std::vector<PluginChainEntry> chain{};

    /*! \brief Instance ID assigned to the next successful insertion. */
    std::string next_instance_id{"instance-id"};

    /*! \brief Optional catalog scan error returned instead of success. */
    std::optional<PluginHostError> next_catalog_scan_error{};

    /*! \brief Progress payloads emitted by the next successful catalog scan. */
    std::vector<PluginCatalogScanProgress> next_catalog_scan_progress{};

    /*! \brief Optional insertion error returned instead of a handle. */
    std::optional<PluginHostError> next_insert_error{};

    /*! \brief Optional error returned after insertion is rolled back to the pre-call chain. */
    std::optional<PluginHostError> next_insert_after_mutation_error{};

    /*! \brief Optional move error returned instead of a snapshot. */
    std::optional<PluginHostError> next_move_error{};

    /*! \brief Optional error returned after move is rolled back to the pre-call chain. */
    std::optional<PluginHostError> next_move_after_mutation_error{};

    /*! \brief Optional removal error returned instead of success. */
    std::optional<PluginHostError> next_remove_error{};

    /*! \brief Optional error returned after removal is rolled back to the pre-call chain. */
    std::optional<PluginHostError> next_remove_after_mutation_error{};

    /*! \brief Optional state-capture error returned instead of captured bytes. */
    std::optional<PluginHostError> next_capture_state_error{};

    /*! \brief Optional state-recreate error returned instead of a restored plugin. */
    std::optional<PluginHostError> next_recreate_state_error{};

    /*! \brief Optional error returned after state recreate is rolled back to pre-call state. */
    std::optional<PluginHostError> next_recreate_state_after_mutation_error{};

    /*! \brief Optional in-place state-restore error returned instead of success. */
    std::optional<PluginHostError> next_set_state_error{};

    /*! \brief Optional error returned after state restore is rolled back to pre-call state. */
    std::optional<PluginHostError> next_set_state_after_mutation_error{};

    /*! \brief Optional open-window error returned instead of success. */
    std::optional<PluginHostError> next_open_error{};

    /*! \brief Last roots passed to scanPluginLocations(). */
    std::vector<std::filesystem::path> last_scan_roots{};

    /*! \brief Last candidate passed to insertPlugin(). */
    std::optional<PluginCandidate> last_inserted_plugin_candidate{};

    /*! \brief Last chain index passed to insertPlugin(). */
    std::optional<std::size_t> last_insert_index{};

    /*! \brief Last instance ID passed to movePlugin(). */
    std::optional<std::string> last_moved_instance_id{};

    /*! \brief Last destination index passed to movePlugin(). */
    std::optional<std::size_t> last_move_destination_index{};

    /*! \brief Last instance ID passed to removePlugin(). */
    std::optional<std::string> last_removed_instance_id{};

    /*! \brief Last instance ID passed to capturePluginState(). */
    std::optional<std::string> last_captured_instance_id{};

    /*! \brief Last state passed to recreatePluginStatePreservingId(). */
    std::optional<PluginInstanceState> last_recreated_state{};

    /*! \brief Last chain index passed to recreatePluginStatePreservingId(). */
    std::optional<std::size_t> last_recreate_state_index{};

    /*! \brief Last instance ID passed to setPluginState(). */
    std::optional<std::string> last_set_state_instance_id{};

    /*! \brief Last state passed to setPluginState(). */
    std::optional<PluginInstanceState> last_set_state{};

    /*! \brief Last instance ID passed to openPluginWindow(). */
    std::optional<std::string> last_opened_instance_id{};

    /*! \brief Number of catalog scan calls received. */
    int catalog_scan_call_count{0};

    /*! \brief True when a catalog scan observed cooperative cancellation and stopped early. */
    bool catalog_scan_canceled{false};

    /*! \brief Number of known-catalog reads received. */
    mutable int known_candidates_call_count{0};

    /*! \brief Number of insertion calls received. */
    int insert_call_count{0};

    /*! \brief Number of move calls received. */
    int move_call_count{0};

    /*! \brief Number of removal calls received. */
    int remove_call_count{0};

    /*! \brief Number of state-capture calls received. */
    int capture_state_call_count{0};

    /*! \brief Number of state-recreate calls received. */
    int recreate_state_call_count{0};

    /*! \brief Number of in-place state-restore calls received. */
    int set_state_call_count{0};

    /*! \brief Number of pending plugin-edit flush calls received. */
    int flush_pending_plugin_edits_call_count{0};

    /*! \brief Number of open-window calls received. */
    int open_call_count{0};

    /*! \brief Pending fake plugin-state edit emitted by the next flush. */
    std::optional<PluginStateEdit> pending_state_edit{};

private:
    // Stores fake state chunks for live instances when a test has applied a state memento.
    std::unordered_map<std::string, PluginInstanceState> m_instance_states{};

    // Holds fake plugin-edit callbacks installed through the public port.
    PluginEditObserver m_plugin_edit_observer{};

    // Holds fake plugin-state-edit callbacks installed through the public port.
    PluginStateEditObserver m_state_edit_observer{};

    // Holds fake plugin-window command callbacks installed through the public port.
    PluginWindowCommandObserver m_window_command_observer{};

    // Appends one field to the test-only state format.
    static void appendStateField(std::string& state, std::string_view value)
    {
        state.append(value);
        state.push_back('\n');
    }

    // Converts a test-only serialized state string into opaque bytes.
    [[nodiscard]] static std::vector<std::byte> bytesFromString(std::string_view text)
    {
        std::vector<std::byte> bytes;
        bytes.reserve(text.size());
        for (const char character : text)
        {
            bytes.push_back(static_cast<std::byte>(static_cast<unsigned char>(character)));
        }
        return bytes;
    }

    // Converts opaque fake state bytes back into the test-only serialized string.
    [[nodiscard]] static std::string stringFromBytes(const std::vector<std::byte>& bytes)
    {
        std::string text;
        text.reserve(bytes.size());
        for (const std::byte byte : bytes)
        {
            text.push_back(static_cast<char>(std::to_integer<unsigned char>(byte)));
        }
        return text;
    }

    // Parses one unsigned size field from the test-only state format.
    [[nodiscard]] static std::optional<std::size_t> parseSizeField(std::string_view text)
    {
        std::size_t value{};
        const auto* const first = text.data();
        const auto* const last = first + text.size();
        const auto [parsed_last, error_code] = std::from_chars(first, last, value);
        if (error_code != std::errc{} || parsed_last != last)
        {
            return std::nullopt;
        }
        return value;
    }

    // Splits the test-only state format into newline-delimited fields.
    [[nodiscard]] static std::vector<std::string> splitStateFields(std::string_view state)
    {
        std::vector<std::string> fields;
        std::size_t field_start = 0;
        while (field_start <= state.size())
        {
            const std::size_t field_end = state.find('\n', field_start);
            if (field_end == std::string_view::npos)
            {
                fields.emplace_back(state.substr(field_start));
                break;
            }
            fields.emplace_back(state.substr(field_start, field_end - field_start));
            field_start = field_end + 1;
        }
        return fields;
    }

    // Serializes all chain-entry fields needed to recreate a fake plugin instance.
    [[nodiscard]] static PluginInstanceState makePluginInstanceState(const PluginChainEntry& entry)
    {
        std::string serialized_state;
        appendStateField(serialized_state, "RecordingPluginHostState1");
        appendStateField(serialized_state, entry.instance_id);
        appendStateField(serialized_state, entry.plugin_id);
        appendStateField(serialized_state, entry.name);
        appendStateField(serialized_state, entry.manufacturer);
        appendStateField(serialized_state, entry.format_name);
        appendStateField(serialized_state, entry.category);
        appendStateField(serialized_state, std::to_string(entry.chain_index));
        appendStateField(serialized_state, std::to_string(entry.block_index));
        appendStateField(serialized_state, entry.display_type_override);
        return PluginInstanceState{.opaque_data = bytesFromString(serialized_state)};
    }

    // Recreates a fake chain entry from its self-contained opaque state bytes.
    [[nodiscard]] static std::optional<PluginChainEntry> pluginChainEntryFromState(
        const PluginInstanceState& state)
    {
        const std::vector<std::string> fields =
            splitStateFields(stringFromBytes(state.opaque_data));
        if (fields.size() != 11 || fields[0] != "RecordingPluginHostState1" ||
            !fields.back().empty())
        {
            return std::nullopt;
        }

        const std::optional<std::size_t> chain_index = parseSizeField(fields[7]);
        const std::optional<std::size_t> block_index = parseSizeField(fields[8]);
        if (!chain_index.has_value() || !block_index.has_value())
        {
            return std::nullopt;
        }

        return PluginChainEntry{
            .instance_id = fields[1],
            .plugin_id = fields[2],
            .name = fields[3],
            .manufacturer = fields[4],
            .format_name = fields[5],
            .category = fields[6],
            .chain_index = *chain_index,
            .block_index = *block_index,
            .display_type_override = fields[9],
        };
    }

    // Reassigns contiguous chain indices after a successful structural mutation.
    void reindexChain()
    {
        for (std::size_t index = 0; index < chain.size(); ++index)
        {
            chain[index].chain_index = index;
        }
    }

    // Returns an authoritative snapshot of the current fake chain.
    [[nodiscard]] PluginChainSnapshot snapshot() const
    {
        return PluginChainSnapshot{.plugins = chain};
    }

    // Finds one fake chain entry by runtime instance ID.
    [[nodiscard]] std::vector<PluginChainEntry>::iterator findPlugin(const std::string& instance_id)
    {
        return std::ranges::find_if(chain, [&instance_id](const PluginChainEntry& plugin) {
            return plugin.instance_id == instance_id;
        });
    }
};

} // namespace rock_hero::common::audio::testing
