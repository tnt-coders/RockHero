#include "engine_impl.h"
#include "live_rig/tone_document.h"
#include "plugin/plugin_scan.h"
#include "shared/audio_path_util.h"
#include "tracktion/live_rig_gain_plugin.h"
#include "tracktion/plugin_dirty_tracking.h"

#include <chrono>
#include <exception>
#include <rock_hero/common/core/shared/juce_path.h>
#include <system_error>
#include <unordered_set>

namespace rock_hero::common::audio
{

// Named so the Impl declaration in engine_impl.h can reference it.
// Maps monitoring rebuild failures into plugin-host mutation errors.
[[nodiscard]] PluginHostError pluginHostErrorFromLiveInputError(const LiveInputError& error)
{
    switch (error.code)
    {
        case LiveInputErrorCode::MessageThreadRequired:
        {
            return PluginHostError{PluginHostErrorCode::MessageThreadRequired, error.message};
        }
        case LiveInputErrorCode::TrackMissing:
        {
            return PluginHostError{PluginHostErrorCode::TrackMissing, error.message};
        }
        default:
        {
            return PluginHostError{PluginHostErrorCode::MonitoringRouteFailed, error.message};
        }
    }
}

namespace
{

constexpr std::string_view g_plugin_scan_command_line_prefix{"--PluginScan:"};
constexpr auto g_plugin_scan_timeout = std::chrono::seconds{30};

// Parses a memento payload into Tracktion's plugin-state tree and rejects non-external states.
[[nodiscard]] std::expected<juce::ValueTree, PluginHostError> pluginStateTreeFromMemento(
    const PluginInstanceState& state)
{
    if (state.opaque_data.empty())
    {
        return std::unexpected{PluginHostError{
            PluginHostErrorCode::PluginStateRestoreFailed,
            "Plugin state is empty",
        }};
    }

    if (state.opaque_data.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()))
    {
        return std::unexpected{PluginHostError{
            PluginHostErrorCode::PluginStateRestoreFailed,
            "Plugin state is too large to parse",
        }};
    }

    const std::string xml_text = stringFromBytes(state.opaque_data);
    const juce::String xml_string =
        juce::String::fromUTF8(xml_text.data(), static_cast<int>(xml_text.size()));
    const std::unique_ptr<juce::XmlElement> xml = juce::parseXML(xml_string);
    if (xml == nullptr)
    {
        return std::unexpected{PluginHostError{
            PluginHostErrorCode::PluginStateRestoreFailed,
            "Could not parse plugin state",
        }};
    }

    juce::ValueTree plugin_state = juce::ValueTree::fromXml(*xml);
    if (!plugin_state.isValid() || !plugin_state.hasType(tracktion::IDs::PLUGIN) ||
        plugin_state[tracktion::IDs::type].toString() != tracktion::ExternalPlugin::xmlTypeName)
    {
        return std::unexpected{PluginHostError{
            PluginHostErrorCode::PluginStateRestoreFailed,
            "Plugin state is not an external plugin ValueTree",
        }};
    }

    return plugin_state;
}

// Reads the runtime instance id encoded in a captured Tracktion plugin tree, when one exists.
[[nodiscard]] std::string pluginInstanceIdFromState(const juce::ValueTree& plugin_state)
{
    const tracktion::EditItemID item_id = tracktion::EditItemID::fromID(plugin_state);
    return item_id.isValid() ? item_id.toString().toStdString() : std::string{};
}

// Removes AUTOMATIONCURVE children so the plugin-chunk undo memento never carries automation
// curves: curves belong to RockHero's separate point-list undo domain (tone_automation_edits), and
// the chunk restore path cannot rebind a live curve, so keeping the two domains disjoint means a
// chunk undo/redo can never clobber an authored automation curve.
void stripAutomationCurves(juce::ValueTree& plugin_state)
{
    for (int index = plugin_state.getNumChildren(); --index >= 0;)
    {
        if (plugin_state.getChild(index).hasType(tracktion::IDs::AUTOMATIONCURVE))
        {
            plugin_state.removeChild(index, nullptr);
        }
    }
}

// Copies serialized plugin state to an existing live plugin without changing its runtime id.
void copyPluginStatePreservingInstanceId(
    tracktion::Plugin& target_plugin, const juce::ValueTree& source_state)
{
    juce::ValueTree target_state = target_plugin.state;

    // Remove only properties the restored state no longer carries, then overwrite the rest in
    // place. juce::ValueTree::setProperty is a no-op when the value is unchanged (NamedValueSet::set
    // compares values - including binary blobs - by content and notifies listeners only on a real
    // change), so invariant properties are left untouched instead of being wiped and re-added. The
    // previous wipe-and-re-add re-applied IDs::layout, whose ExternalPlugin listener re-prepares the
    // plugin (releaseResources + prepareToPlay); that fired once on removal and again on re-add
    // (~74ms total) and was the entire undo/redo visual dropout.
    for (int index = target_state.getNumProperties(); --index >= 0;)
    {
        const juce::Identifier property_name = target_state.getPropertyName(index);
        if (property_name != tracktion::IDs::id && !source_state.hasProperty(property_name))
        {
            target_state.removeProperty(property_name, nullptr);
        }
    }

    for (int index = 0; index < source_state.getNumProperties(); ++index)
    {
        const juce::Identifier property_name = source_state.getPropertyName(index);
        if (property_name != tracktion::IDs::id)
        {
            target_state.setProperty(
                property_name, source_state.getProperty(property_name), nullptr);
        }
    }

    // Preserve any live AUTOMATIONCURVE children. Automation curves are timeline data owned by
    // RockHero's own point-list undo (see tone_automation_edits), not plugin-parameter state, and
    // the plugin-chunk memento never carries them (see capturePluginState). Wiping them here would
    // strand the parameter's live curve binding, because the runtime rebind only re-points a curve
    // on an IDs::name==paramID child add, never on an AUTOMATIONCURVE add.
    for (int index = target_state.getNumChildren(); --index >= 0;)
    {
        if (!target_state.getChild(index).hasType(tracktion::IDs::AUTOMATIONCURVE))
        {
            target_state.removeChild(index, nullptr);
        }
    }
    int insert_index = 0;
    const int source_child_count = source_state.getNumChildren();
    for (int index = 0; index < source_child_count; ++index)
    {
        const juce::ValueTree source_child = source_state.getChild(index);
        if (!source_child.hasType(tracktion::IDs::AUTOMATIONCURVE))
        {
            target_state.addChild(source_child.createCopy(), insert_index++, nullptr);
        }
    }

    target_plugin.itemID.writeID(target_state, nullptr);
}

} // namespace

std::unique_ptr<juce::PluginDescription> Engine::Impl::findKnownPlugin(
    const std::string& plugin_id) const
{
    return m_engine->getPluginManager().knownPluginList.getTypeForIdentifierString(
        juce::String{plugin_id});
}

std::expected<std::vector<PluginCandidate>, PluginHostError> Engine::Impl::
    scanPluginFileForCandidates(const std::filesystem::path& plugin_path)
{
    const std::filesystem::path scan_path = vst3DisplayPath(plugin_path).lexically_normal();
    const juce::File plugin_file = common::core::juceFileFromPath(scan_path);
    if (scan_path.empty() || !plugin_file.exists())
    {
        return std::unexpected{PluginHostError{
            PluginHostErrorCode::MissingPluginFile,
            "Plugin file does not exist: " + scan_path.string()
        }};
    }

    try
    {
        constexpr auto* vst3_format_name = "VST3";
        auto& plugin_manager = m_engine->getPluginManager();
        bool scan_session_finished = false;
        const auto finish_scan_session = [&plugin_manager, &scan_session_finished] {
            if (!scan_session_finished)
            {
                plugin_manager.knownPluginList.scanFinished();
                scan_session_finished = true;
            }
        };
        const juce::String& file_or_identifier = plugin_file.getFullPathName();
        juce::OwnedArray<juce::PluginDescription> found_descriptions;
        bool has_vst3_format = false;

        for (juce::AudioPluginFormat* const format :
             plugin_manager.pluginFormatManager.getFormats())
        {
            if (format == nullptr || format->getName() != vst3_format_name)
            {
                continue;
            }

            has_vst3_format = true;
            if (!format->fileMightContainThisPluginType(file_or_identifier))
            {
                continue;
            }

            PluginScanTimeout scan_timeout{
                [&plugin_manager] { plugin_manager.abortCurrentPluginScan(); },
                g_plugin_scan_timeout
            };
            plugin_manager.knownPluginList.scanAndAddFile(
                file_or_identifier, true, found_descriptions, *format);
            scan_timeout.finish();

            if (scan_timeout.timedOut())
            {
                plugin_manager.knownPluginList.removeFromBlacklist(file_or_identifier);
                finish_scan_session();
                return std::unexpected{PluginHostError{
                    PluginHostErrorCode::PluginScanFailed,
                    "Plugin scan timed out after " + std::to_string(g_plugin_scan_timeout.count()) +
                        " seconds: " + scan_path.string()
                }};
            }
        }

        if (!has_vst3_format)
        {
            finish_scan_session();
            return std::unexpected{PluginHostError{
                PluginHostErrorCode::PluginScanFailed,
                "VST3 plugin hosting is not enabled in this build"
            }};
        }

        finish_scan_session();

        std::vector<PluginCandidate> plugin_candidates;
        plugin_candidates.reserve(static_cast<std::size_t>(found_descriptions.size()));

        for (const juce::PluginDescription* description : found_descriptions)
        {
            if (description != nullptr && description->pluginFormatName == vst3_format_name)
            {
                plugin_candidates.push_back(makePluginCandidate(
                    *description, pluginPathFromIdentifier(description->fileOrIdentifier)));
            }
        }

        if (plugin_candidates.empty())
        {
            return std::unexpected{PluginHostError{
                PluginHostErrorCode::NoCompatiblePlugin,
                "No VST3 plugin was found in: " + scan_path.string()
            }};
        }

        return plugin_candidates;
    }
    catch (const std::exception& error)
    {
        m_engine->getPluginManager().knownPluginList.scanFinished();
        return std::unexpected{PluginHostError{
            PluginHostErrorCode::PluginScanFailed,
            std::string{"Plugin scan failed: "} + error.what()
        }};
    }
}

juce::AudioPluginFormat* Engine::Impl::vst3PluginFormat() const
{
    constexpr auto* vst3_format_name = "VST3";
    for (juce::AudioPluginFormat* const format :
         m_engine->getPluginManager().pluginFormatManager.getFormats())
    {
        if (format != nullptr && format->getName() == vst3_format_name)
        {
            return format;
        }
    }

    return nullptr;
}

juce::File Engine::Impl::pluginScanDeadMansPedalFile() const
{
    return m_engine->getPropertyStorage().getAppCacheFolder().getChildFile(
        "PluginScanDeadMansPedal.txt");
}

juce::FileSearchPath Engine::Impl::pluginSearchPathFromRoots(
    const std::vector<std::filesystem::path>& roots)
{
    juce::FileSearchPath search_path;
    for (const std::filesystem::path& root : roots)
    {
        if (!root.empty())
        {
            search_path.addIfNotAlreadyThere(common::core::juceFileFromPath(root));
        }
    }

    search_path.removeRedundantPaths();
    return search_path;
}

std::filesystem::path Engine::Impl::pluginPathFromIdentifier(const juce::String& file_or_identifier)
{
    return common::core::pathFromJuceString(file_or_identifier);
}

std::expected<juce::StringArray, PluginHostError> Engine::Impl::scanVst3SearchPath(
    juce::FileSearchPath search_path, const PluginCatalogScanProgressCallback& progress_callback,
    const common::core::CancellationToken& cancel)
{
    juce::AudioPluginFormat* const format = vst3PluginFormat();
    if (format == nullptr)
    {
        return std::unexpected{PluginHostError{
            PluginHostErrorCode::PluginScanFailed,
            "VST3 plugin hosting is not enabled in this build"
        }};
    }

    const auto scan_started_at = std::chrono::steady_clock::now();
    search_path.removeRedundantPaths();
    juce::StringArray files = format->searchPathsForPlugins(search_path, true, true);
    files.removeEmptyStrings();
    files.removeDuplicates(true);
    const auto total_plugins = static_cast<std::size_t>(files.size());

    try
    {
        auto& plugin_manager = m_engine->getPluginManager();
        juce::PluginDirectoryScanner scanner{
            plugin_manager.knownPluginList,
            *format,
            juce::FileSearchPath{},
            true,
            pluginScanDeadMansPedalFile(),
            true
        };
        scanner.setFilesOrIdentifiersToScan(files);

        // Progress is reported before scanning so the active path names the file about to be
        // validated. Asking the scanner for the next file keeps the path and any timeout
        // message aligned with its own dead-man-pedal reordering. For VST3 the returned
        // identifier is the file path.
        for (std::size_t completed_plugins = 0; completed_plugins < total_plugins;
             ++completed_plugins)
        {
            // Stop at the next candidate boundary on cancellation. Candidates already validated
            // stay in the known-plugin list, so a cancelled scan still keeps partial progress.
            if (cancel.isCancelled())
            {
                break;
            }

            const juce::String file_or_identifier = scanner.getNextPluginFileThatWillBeScanned();
            reportPluginCatalogScanProgress(
                progress_callback,
                completed_plugins,
                total_plugins,
                pluginPathFromIdentifier(file_or_identifier));

            juce::String name_of_plugin_being_scanned;
            PluginScanTimeout scan_timeout{
                [&plugin_manager] { plugin_manager.abortCurrentPluginScan(); },
                g_plugin_scan_timeout
            };
            scanner.scanNextFile(true, name_of_plugin_being_scanned);
            scan_timeout.finish();

            if (scan_timeout.timedOut())
            {
                plugin_manager.knownPluginList.removeFromBlacklist(file_or_identifier);
                return std::unexpected{PluginHostError{
                    PluginHostErrorCode::PluginScanFailed,
                    "Plugin scan timed out after " + std::to_string(g_plugin_scan_timeout.count()) +
                        " seconds: " + file_or_identifier.toStdString()
                }};
            }
        }

        reportPluginCatalogScanProgress(progress_callback, total_plugins, total_plugins, {});
    }
    catch (const std::exception& error)
    {
        return std::unexpected{PluginHostError{
            PluginHostErrorCode::PluginScanFailed,
            std::string{"Plugin catalog scan failed: "} + error.what()
        }};
    }

    logPluginCatalogScanSummary(total_plugins, elapsedMilliseconds(scan_started_at));
    return files;
}

std::vector<PluginCandidate> Engine::Impl::knownPluginCatalog() const
{
    constexpr auto* vst3_format_name = "VST3";
    std::vector<PluginCandidate> plugin_candidates;
    std::unordered_set<std::string> seen_plugin_ids;
    std::unordered_set<std::string> seen_plugin_paths;
    const auto& known_types = m_engine->getPluginManager().knownPluginList.getTypes();
    plugin_candidates.reserve(static_cast<std::size_t>(known_types.size()));
    seen_plugin_ids.reserve(plugin_candidates.capacity());
    seen_plugin_paths.reserve(plugin_candidates.capacity());

    const auto append_candidate = [&plugin_candidates, &seen_plugin_ids, &seen_plugin_paths](
                                      PluginCandidate plugin_candidate) {
        const std::string path_key = normalizedPluginPathKey(plugin_candidate.file_path);
        if (seen_plugin_ids.contains(plugin_candidate.id) || seen_plugin_paths.contains(path_key))
        {
            return;
        }

        seen_plugin_ids.insert(plugin_candidate.id);
        seen_plugin_paths.insert(path_key);
        plugin_candidates.push_back(std::move(plugin_candidate));
    };

    for (const juce::PluginDescription& description : known_types)
    {
        if (description.pluginFormatName != vst3_format_name)
        {
            continue;
        }

        append_candidate(makePluginCandidate(
            description, pluginPathFromIdentifier(description.fileOrIdentifier)));
    }

    return plugin_candidates;
}

std::vector<PluginCandidate> Engine::Impl::knownPluginCatalogForScannedFiles(
    const juce::StringArray& scanned_files) const
{
    std::unordered_set<std::string> scanned_paths;
    scanned_paths.reserve(static_cast<std::size_t>(scanned_files.size()));
    for (const juce::String& file_or_identifier : scanned_files)
    {
        scanned_paths.insert(normalizedPluginPathKey(pluginPathFromIdentifier(file_or_identifier)));
    }

    std::vector<PluginCandidate> plugin_candidates;
    for (PluginCandidate plugin_candidate : knownPluginCatalog())
    {
        if (scanned_paths.contains(normalizedPluginPathKey(plugin_candidate.file_path)))
        {
            plugin_candidates.push_back(std::move(plugin_candidate));
        }
    }

    return plugin_candidates;
}

// The user-visible chain is the audible tone's rack branch: the panel binds to the selected
// tone, and only the selected tone is editable.
PluginChainSnapshot Engine::Impl::pluginChainSnapshot() const
{
    PluginChainSnapshot snapshot;
    const ToneRackBranch* const branch = audibleToneBranch();
    if (branch == nullptr)
    {
        return snapshot;
    }

    snapshot.plugins.reserve(branch->chain.size());
    for (const tracktion::Plugin::Ptr& plugin : branch->chain)
    {
        if (plugin == nullptr)
        {
            continue;
        }

        snapshot.plugins.push_back(makePluginChainEntry(*plugin, snapshot.plugins.size()));
    }

    return snapshot;
}

std::size_t Engine::Impl::userVisiblePluginCount(const tracktion::Plugin* ignored_plugin) const
{
    const ToneRackBranch* const branch = audibleToneBranch();
    if (branch == nullptr)
    {
        return 0;
    }

    std::size_t count = 0;
    for (const tracktion::Plugin::Ptr& plugin : branch->chain)
    {
        if (plugin != nullptr && plugin.get() != ignored_plugin)
        {
            ++count;
        }
    }
    return count;
}

std::optional<std::size_t> Engine::Impl::userVisiblePluginIndexOf(
    const tracktion::Plugin* target_plugin) const
{
    if (target_plugin == nullptr)
    {
        return std::nullopt;
    }

    const ToneRackBranch* const branch = audibleToneBranch();
    if (branch == nullptr)
    {
        return std::nullopt;
    }

    for (std::size_t index = 0; index < branch->chain.size(); ++index)
    {
        if (branch->chain[index].get() == target_plugin)
        {
            return index;
        }
    }

    return std::nullopt;
}

bool Engine::Impl::hasKnownPluginForIdentity(const PluginIdentity& identity) const
{
    const juce::PluginDescription persisted_description = makePluginDescription(identity);
    auto& known_plugin_list = m_engine->getPluginManager().knownPluginList;
    if (!identity.juce_identifier_hint.empty() &&
        known_plugin_list
                .getTypeForIdentifierString(
                    juce::String::fromUTF8(identity.juce_identifier_hint.c_str()))
                .get() != nullptr)
    {
        return true;
    }

    if (!identity.tracktion_identifier_hint.empty() &&
        known_plugin_list
                .getTypeForIdentifierString(
                    juce::String::fromUTF8(identity.tracktion_identifier_hint.c_str()))
                .get() != nullptr)
    {
        return true;
    }

    for (const juce::PluginDescription& known_description : known_plugin_list.getTypes())
    {
        if (persisted_description.isDuplicateOf(known_description))
        {
            return true;
        }
    }

    return false;
}

std::expected<void, LiveRigError> Engine::Impl::ensureKnownPluginForIdentity(
    const PluginIdentity& identity)
{
    if (hasKnownPluginForIdentity(identity))
    {
        return {};
    }

    if (!identity.original_file_or_identifier.empty())
    {
        const std::filesystem::path plugin_path{identity.original_file_or_identifier};
        std::error_code error;
        if (std::filesystem::exists(plugin_path, error))
        {
            const auto scan_result = scanPluginFileForCandidates(plugin_path);
            if (!scan_result.has_value())
            {
                return std::unexpected{LiveRigError{
                    LiveRigErrorCode::PluginScanFailed,
                    scan_result.error().message,
                }};
            }

            if (hasKnownPluginForIdentity(identity))
            {
                return {};
            }
        }
    }

    return std::unexpected{LiveRigError{
        LiveRigErrorCode::PluginNotFound, "Tone plugin was not found: " + identity.name
    }};
}

// Searches every tone branch, not just the audible one: undo restores and open plugin windows
// may target plugins from tones that are no longer selected.
tracktion::Plugin* Engine::Impl::findInstrumentPluginInstance(const std::string& instance_id) const
{
    if (!m_tone_rack.has_value())
    {
        return nullptr;
    }

    const juce::String target_id{instance_id};
    for (const ToneRackBranch& branch : m_tone_rack->branches)
    {
        for (const tracktion::Plugin::Ptr& plugin : branch.chain)
        {
            if (plugin != nullptr && plugin->itemID.toString() == target_id)
            {
                return plugin.get();
            }
        }
    }

    return nullptr;
}

void Engine::Impl::commitPluginRemoval(tracktion::Plugin& plugin) const
{
    if (auto* const macro_parameters = plugin.getMacroParameterList(); macro_parameters != nullptr)
    {
        macro_parameters->hideMacroParametersFromTracks();
    }

    for (tracktion::Track* const track : tracktion::getAllTracks(*m_edit))
    {
        if (track != nullptr)
        {
            track->hideAutomatableParametersForSource(plugin.itemID);
        }
    }

    plugin.hideWindowForShutdown();
    plugin.deselect();
}

bool Engine::Impl::hasPendingPluginEdits() const
{
    const bool has_state_edit = std::ranges::any_of(
        m_plugin_state_trackers, [](const std::unique_ptr<PluginDirtyStateTracker>& tracker) {
            return tracker != nullptr && tracker->hasPendingEdit();
        });
    return has_state_edit;
}

void Engine::Impl::notifyPluginEditPendingStateChanged()
{
    const bool pending = hasPendingPluginEdits();
    if (pending == m_plugin_edit_pending_notified)
    {
        return;
    }

    m_plugin_edit_pending_notified = pending;
    if (m_plugin_edit_observer.pending_changed)
    {
        m_plugin_edit_observer.pending_changed(pending);
    }
}

bool Engine::Impl::shouldDeferPluginUndoCapture() const
{
    return m_plugin_undo_capture_deferred;
}

void Engine::Impl::clearPluginUndoCaptureDeferral()
{
    m_plugin_undo_capture_deferred = false;
}

void Engine::Impl::beginPluginUndoCaptureDeferral()
{
    m_plugin_undo_capture_deferred = true;
}

void Engine::Impl::endPluginUndoCaptureDeferral(bool absorb_reannounce)
{
    clearPluginUndoCaptureDeferral();
    refreshPluginEditObservers(std::nullopt, absorb_reannounce);
}

void Engine::Impl::emitPluginStateEdit(PluginStateEdit edit)
{
    if (shouldDeferPluginUndoCapture())
    {
        return;
    }

    if (m_plugin_state_edit_observer.edit_completed)
    {
        m_plugin_state_edit_observer.edit_completed(std::move(edit));
    }
}

void Engine::Impl::dispatchPluginWindowCommand(PluginWindowCommand command)
{
    switch (command)
    {
        case PluginWindowCommand::Undo:
        {
            if (m_plugin_window_command_observer.undo_requested)
            {
                m_plugin_window_command_observer.undo_requested();
            }
            break;
        }
        case PluginWindowCommand::Redo:
        {
            if (m_plugin_window_command_observer.redo_requested)
            {
                m_plugin_window_command_observer.redo_requested();
            }
            break;
        }
        case PluginWindowCommand::PlayPause:
        {
            if (m_plugin_window_command_observer.play_pause_requested)
            {
                m_plugin_window_command_observer.play_pause_requested();
            }
            break;
        }
    }
}

void Engine::Impl::clearPluginEditObservers()
{
    m_plugin_parameter_dirty_trackers.clear();
    m_plugin_state_trackers.clear();
    notifyPluginEditPendingStateChanged();
}

void Engine::Impl::refreshPluginEditObservers(
    std::optional<KnownPluginBaseline> known_baseline, bool absorb_initial_reannounce)
{
    m_plugin_parameter_dirty_trackers.clear();
    m_plugin_state_trackers.clear();
    if (!m_tone_rack.has_value())
    {
        notifyPluginEditPendingStateChanged();
        return;
    }

    // Observe every branch, not just the audible one: plugin windows opened before a selection
    // switch stay live, and their edits must keep flowing into dirty tracking and undo.
    std::vector<tracktion::Plugin*> observed_plugins;
    for (const ToneRackBranch& branch : m_tone_rack->branches)
    {
        for (const tracktion::Plugin::Ptr& plugin : branch.chain)
        {
            if (plugin != nullptr)
            {
                observed_plugins.push_back(plugin.get());
            }
        }
    }

    for (tracktion::Plugin* const plugin : observed_plugins)
    {
        auto* const external_plugin = dynamic_cast<tracktion::ExternalPlugin*>(plugin);
        if (external_plugin == nullptr)
        {
            continue;
        }

        std::optional<PluginInstanceState> initial_baseline;
        if (known_baseline.has_value() &&
            known_baseline->instance_id == external_plugin->itemID.toString().toStdString())
        {
            initial_baseline = known_baseline->state;
        }

        auto state_tracker = std::make_unique<PluginDirtyStateTracker>(
            *external_plugin,
            [](tracktion::ExternalPlugin& observed_plugin)
                -> std::expected<PluginInstanceState, PluginHostError> {
                observed_plugin.flushPluginStateToValueTree();
                auto state = makePluginInstanceState(observed_plugin.state.createCopy());
                if (!state.has_value())
                {
                    return std::unexpected{std::move(state.error())};
                }

                return std::move(*state);
            },
            [this](PluginStateEdit edit) { emitPluginStateEdit(std::move(edit)); },
            [this] { notifyPluginEditPendingStateChanged(); },
            [this] { return shouldDeferPluginUndoCapture(); },
            std::move(initial_baseline),
            absorb_initial_reannounce);
        // The pointer targets the heap object owned by the unique_ptr, so vector growth does
        // not invalidate the callback target. Parameter trackers are cleared before state
        // trackers, so their callbacks cannot outlive the target.
        PluginDirtyStateTracker* const state_tracker_ptr = state_tracker.get();
        m_plugin_state_trackers.push_back(std::move(state_tracker));
        m_plugin_parameter_dirty_trackers.push_back(
            std::make_unique<PluginParameterDirtyTracker>(
                *external_plugin, [state_tracker_ptr] { state_tracker_ptr->markDirty(); }));
    }

    notifyPluginEditPendingStateChanged();
}

void Engine::Impl::refreshRestoredPluginEditObserver(
    const std::string& instance_id, PluginInstanceState restored_state)
{
    tracktion::Plugin* const plugin = findInstrumentPluginInstance(instance_id);
    auto* const external_plugin = dynamic_cast<tracktion::ExternalPlugin*>(plugin);
    if (external_plugin == nullptr)
    {
        refreshPluginEditObservers(
            KnownPluginBaseline{
                .instance_id = instance_id,
                .state = std::move(restored_state),
            });
        return;
    }

    for (std::size_t index = 0; index < m_plugin_state_trackers.size(); ++index)
    {
        PluginDirtyStateTracker* const state_tracker = m_plugin_state_trackers[index].get();
        if (state_tracker == nullptr || state_tracker->instanceId() != instance_id)
        {
            continue;
        }

        state_tracker->resetBaseline(std::move(restored_state));
        if (index < m_plugin_parameter_dirty_trackers.size())
        {
            m_plugin_parameter_dirty_trackers[index].reset();
            m_plugin_parameter_dirty_trackers[index] =
                std::make_unique<PluginParameterDirtyTracker>(
                    *external_plugin, [state_tracker] { state_tracker->markDirty(); });
        }
        notifyPluginEditPendingStateChanged();
        return;
    }

    refreshPluginEditObservers(
        KnownPluginBaseline{
            .instance_id = instance_id,
            .state = std::move(restored_state),
        });
}

void Engine::Impl::flushPendingPluginEdits()
{
    for (const std::unique_ptr<PluginDirtyStateTracker>& tracker : m_plugin_state_trackers)
    {
        if (tracker != nullptr)
        {
            tracker->flushPendingEdit();
        }
    }
    notifyPluginEditPendingStateChanged();
}

// Handles the child-process entry point used by Tracktion's isolated plugin scanner.
bool Engine::startPluginScanChildProcess(std::string_view command_line)
{
    return tracktion::PluginManager::startChildProcessPluginScan(toJuceString(command_line));
}

// Checks whether a command line is addressed to Tracktion's isolated plugin scanner.
bool Engine::isPluginScanChildProcessCommandLine(std::string_view command_line)
{
    return toJuceString(command_line)
        .trim()
        .startsWith(toJuceString(g_plugin_scan_command_line_prefix));
}

// Scans JUCE's default VST3 roots through Tracktion's known-plugin list. Tracktion persists the
// resulting descriptions, so repeated scans can reuse unchanged entries.
std::expected<void, PluginHostError> Engine::scanPluginCatalog(
    PluginCatalogScanProgressCallback progress_callback,
    const common::core::CancellationToken& cancel)
{
    juce::AudioPluginFormat* const format = m_impl->vst3PluginFormat();
    if (format == nullptr)
    {
        return std::unexpected{PluginHostError{
            PluginHostErrorCode::PluginScanFailed,
            "VST3 plugin hosting is not enabled in this build"
        }};
    }

    auto scanned_files = m_impl->scanVst3SearchPath(
        format->getDefaultLocationsToSearch(), progress_callback, cancel);
    if (!scanned_files.has_value())
    {
        return std::unexpected{std::move(scanned_files.error())};
    }

    return {};
}

// Scans user-supplied VST3 locations through Tracktion's known-plugin list.
std::expected<std::vector<PluginCandidate>, PluginHostError> Engine::scanPluginLocations(
    const std::vector<std::filesystem::path>& roots,
    PluginCatalogScanProgressCallback progress_callback)
{
    auto scanned_files =
        m_impl->scanVst3SearchPath(Impl::pluginSearchPathFromRoots(roots), progress_callback);
    if (!scanned_files.has_value())
    {
        return std::unexpected{std::move(scanned_files.error())};
    }

    return m_impl->knownPluginCatalogForScannedFiles(*scanned_files);
}

// Reads Tracktion's known-plugin list without launching plugin scanners.
std::vector<PluginCandidate> Engine::knownPluginCatalog() const
{
    return m_impl->knownPluginCatalog();
}

// Inserts a selected VST3 candidate into the instrument track's user-visible plugin chain.
std::expected<PluginInsertResult, PluginHostError> Engine::Impl::insertPluginCandidateToTrack(
    const PluginCandidate& plugin_candidate, std::size_t chain_index)
{
    if (!juce::MessageManager::getInstance()->isThisTheMessageThread())
    {
        return std::unexpected{PluginHostError{PluginHostErrorCode::MessageThreadRequired}};
    }

    std::unique_ptr<juce::PluginDescription> description = findKnownPlugin(plugin_candidate.id);
    std::string resolved_plugin_id = plugin_candidate.id;
    if (description == nullptr)
    {
        if (plugin_candidate.file_path.empty())
        {
            return std::unexpected{PluginHostError{
                PluginHostErrorCode::PluginNotFound,
                "Plugin candidate was not found: " + plugin_candidate.id
            }};
        }

        const auto validation_started_at = std::chrono::steady_clock::now();
        const auto scan_result = scanPluginFileForCandidates(plugin_candidate.file_path);
        logPluginValidationSummary(
            plugin_candidate.file_path,
            elapsedMilliseconds(validation_started_at),
            scan_result.has_value() ? std::optional<std::string>{}
                                    : std::optional<std::string>{scan_result.error().message});
        if (!scan_result.has_value())
        {
            return std::unexpected{scan_result.error()};
        }

        resolved_plugin_id = scan_result->front().id;
        description = findKnownPlugin(resolved_plugin_id);
        if (description == nullptr)
        {
            return std::unexpected{PluginHostError{
                PluginHostErrorCode::PluginNotFound,
                "Plugin candidate was not found after scanning: " +
                    pathToUtf8String(plugin_candidate.file_path)
            }};
        }
    }

    const std::optional<std::size_t> branch_index = audibleBranchIndex();
    if (!branch_index.has_value())
    {
        return std::unexpected{PluginHostError{PluginHostErrorCode::TrackMissing}};
    }

    const std::size_t plugin_count = userVisiblePluginCount();
    if (chain_index > plugin_count)
    {
        return std::unexpected{PluginHostError{PluginHostErrorCode::InvalidChainIndex}};
    }

    if (plugin_count >= g_max_signal_chain_plugins)
    {
        return std::unexpected{PluginHostError{
            PluginHostErrorCode::PluginChainLimitExceeded,
            pluginChainLimitExceededMessage(plugin_count),
        }};
    }
    tracktion::Plugin::Ptr plugin;
    auto mutation_result = mutateAndReroutePluginChain(
        [this, &description, branch_index, chain_index, &plugin]
        -> std::expected<void, PluginChainMutationFailure> {
            plugin = m_edit->getPluginCache().createNewPlugin(
                tracktion::ExternalPlugin::xmlTypeName, *description);
            if (plugin == nullptr)
            {
                return std::unexpected{PluginChainMutationFailure{
                    .error =
                        PluginHostError{
                            PluginHostErrorCode::PluginCreationFailed,
                            "Could not create plugin: " + description->name.toStdString(),
                        },
                    .reroute_context = "plugin creation rollback failed",
                }};
            }

            if (auto* const external_plugin =
                    dynamic_cast<tracktion::ExternalPlugin*>(plugin.get());
                external_plugin != nullptr)
            {
                const juce::String load_error = external_plugin->getLoadError();
                if (load_error.isNotEmpty())
                {
                    return std::unexpected{PluginChainMutationFailure{
                        .error =
                            PluginHostError{
                                PluginHostErrorCode::PluginLoadFailed,
                                load_error.toStdString(),
                            },
                        .reroute_context = "plugin load rollback failed",
                    }};
                }
            }

            if (auto inserted = insertIntoBranch(*m_tone_rack, *branch_index, chain_index, plugin);
                !inserted.has_value())
            {
                return std::unexpected{PluginChainMutationFailure{
                    .error =
                        PluginHostError{
                            PluginHostErrorCode::PluginInsertionFailed,
                            std::move(inserted.error().message),
                        },
                    .reroute_context = "plugin insertion rollback failed",
                }};
            }

            return {};
        },
        [this, branch_index, &plugin] {
            const std::optional<std::size_t> inserted_index =
                userVisiblePluginIndexOf(plugin.get());
            if (inserted_index.has_value() &&
                !removeFromBranch(*m_tone_rack, *branch_index, *inserted_index).has_value())
            {
                logInstrumentMonitoringFailure("plugin insertion rollback could not unwire");
            }
        },
        "plugin insertion route rollback failed");
    if (!mutation_result.has_value())
    {
        return std::unexpected{std::move(mutation_result.error())};
    }

    auto snapshot = pluginChainSnapshot();
    for (PluginChainEntry& entry : snapshot.plugins)
    {
        if (entry.instance_id == plugin->itemID.toString().toStdString())
        {
            entry.plugin_id = resolved_plugin_id;
            break;
        }
    }

    return PluginInsertResult{
        .snapshot = std::move(snapshot),
        .inserted_instance_id = plugin->itemID.toString().toStdString(),
    };
}

// Inserts a selected VST3 candidate into the instrument track's user-visible plugin chain.
std::expected<PluginInsertResult, PluginHostError> Engine::insertPlugin(
    const PluginCandidate& plugin_candidate, std::size_t chain_index)
{
    return m_impl->insertPluginCandidateToTrack(plugin_candidate, chain_index);
}

// Moves a loaded plugin inside the instrument track and rebuilds monitoring around the mutation.
std::expected<PluginChainSnapshot, PluginHostError> Engine::movePlugin(
    const std::string& instance_id, std::size_t destination_index)
{
    if (!juce::MessageManager::getInstance()->isThisTheMessageThread())
    {
        return std::unexpected{PluginHostError{PluginHostErrorCode::MessageThreadRequired}};
    }

    const tracktion::Plugin* const plugin = m_impl->findInstrumentPluginInstance(instance_id);
    if (plugin == nullptr)
    {
        return std::unexpected{PluginHostError{
            PluginHostErrorCode::PluginInstanceNotFound,
            "Plugin instance was not found: " + instance_id
        }};
    }

    const std::optional<std::size_t> branch_index = m_impl->audibleBranchIndex();
    if (!branch_index.has_value())
    {
        return std::unexpected{PluginHostError{PluginHostErrorCode::TrackMissing}};
    }

    const std::size_t plugin_count = m_impl->userVisiblePluginCount();
    if (destination_index >= plugin_count)
    {
        return std::unexpected{PluginHostError{PluginHostErrorCode::InvalidChainIndex}};
    }

    const std::optional<std::size_t> current_index = m_impl->userVisiblePluginIndexOf(plugin);
    if (!current_index.has_value())
    {
        // The plugin exists but lives on a non-audible branch, which the panel cannot move.
        return std::unexpected{PluginHostError{
            PluginHostErrorCode::PluginInstanceNotFound,
            "Plugin instance was not found: " + instance_id
        }};
    }

    if (*current_index == destination_index)
    {
        return m_impl->pluginChainSnapshot();
    }

    auto mutation_result = m_impl->mutateAndReroutePluginChain(
        [this, branch_index, current_index, destination_index]
        -> std::expected<void, PluginChainMutationFailure> {
            if (auto moved = moveWithinBranch(
                    *m_impl->m_tone_rack, *branch_index, *current_index, destination_index);
                !moved.has_value())
            {
                return std::unexpected{PluginChainMutationFailure{
                    .error =
                        PluginHostError{
                            PluginHostErrorCode::PluginMoveFailed,
                            std::move(moved.error().message),
                        },
                    .reroute_context = "plugin move rollback failed",
                }};
            }

            return {};
        },
        [this, branch_index, current_index, destination_index] {
            if (!moveWithinBranch(
                     *m_impl->m_tone_rack, *branch_index, destination_index, *current_index)
                     .has_value())
            {
                logInstrumentMonitoringFailure("plugin move rollback could not rewire");
            }
        },
        "plugin move route rollback failed");
    if (!mutation_result.has_value())
    {
        return std::unexpected{std::move(mutation_result.error())};
    }

    return m_impl->pluginChainSnapshot();
}

// Removes a loaded plugin from the instrument track and rebuilds monitoring around the mutation.
std::expected<PluginChainSnapshot, PluginHostError> Engine::removePlugin(
    const std::string& instance_id)
{
    if (!juce::MessageManager::getInstance()->isThisTheMessageThread())
    {
        return std::unexpected{PluginHostError{PluginHostErrorCode::MessageThreadRequired}};
    }

    tracktion::Plugin* const plugin = m_impl->findInstrumentPluginInstance(instance_id);
    if (plugin == nullptr)
    {
        return std::unexpected{PluginHostError{
            PluginHostErrorCode::PluginInstanceNotFound,
            "Plugin instance was not found: " + instance_id
        }};
    }

    const std::optional<std::size_t> branch_index = m_impl->audibleBranchIndex();
    if (!branch_index.has_value())
    {
        return std::unexpected{PluginHostError{PluginHostErrorCode::TrackMissing}};
    }

    const std::optional<std::size_t> current_index = m_impl->userVisiblePluginIndexOf(plugin);
    if (!current_index.has_value())
    {
        // The plugin exists but lives on a non-audible branch, which the panel cannot remove.
        return std::unexpected{PluginHostError{
            PluginHostErrorCode::PluginInstanceNotFound,
            "Plugin instance was not found: " + instance_id
        }};
    }

    const tracktion::Plugin::Ptr removed_plugin{plugin};
    auto mutation_result = m_impl->mutateAndReroutePluginChain(
        [this, branch_index, current_index] -> std::expected<void, PluginChainMutationFailure> {
            if (auto removed =
                    removeFromBranch(*m_impl->m_tone_rack, *branch_index, *current_index);
                !removed.has_value())
            {
                return std::unexpected{PluginChainMutationFailure{
                    .error =
                        PluginHostError{
                            PluginHostErrorCode::PluginRemovalFailed,
                            std::move(removed.error().message),
                        },
                    .reroute_context = "plugin removal rollback failed",
                }};
            }

            return {};
        },
        [this, branch_index, current_index, removed_plugin] {
            if (!insertIntoBranch(
                     *m_impl->m_tone_rack, *branch_index, *current_index, removed_plugin)
                     .has_value())
            {
                logInstrumentMonitoringFailure("plugin removal rollback could not rewire");
            }
        },
        "plugin removal route rollback failed");
    if (!mutation_result.has_value())
    {
        return std::unexpected{std::move(mutation_result.error())};
    }
    m_impl->commitPluginRemoval(*removed_plugin);
    return m_impl->pluginChainSnapshot();
}

// Captures a user plugin's Tracktion state into an opaque editor-core memento.
std::expected<PluginInstanceState, PluginHostError> Engine::capturePluginState(
    const std::string& instance_id)
{
    if (!juce::MessageManager::getInstance()->isThisTheMessageThread())
    {
        return std::unexpected{PluginHostError{PluginHostErrorCode::MessageThreadRequired}};
    }

    if (m_impl->instrumentTrack() == nullptr)
    {
        return std::unexpected{PluginHostError{PluginHostErrorCode::TrackMissing}};
    }

    tracktion::Plugin* const plugin = m_impl->findInstrumentPluginInstance(instance_id);
    if (plugin == nullptr || m_impl->isStructuralLiveRigPlugin(plugin))
    {
        return std::unexpected{PluginHostError{
            PluginHostErrorCode::PluginInstanceNotFound,
            "Plugin instance was not found: " + instance_id,
        }};
    }

    auto* const external_plugin = dynamic_cast<tracktion::ExternalPlugin*>(plugin);
    if (external_plugin == nullptr)
    {
        return std::unexpected{PluginHostError{
            PluginHostErrorCode::PluginStateCaptureFailed,
            "Only external plugins can be captured right now: " + plugin->getName().toStdString(),
        }};
    }

    external_plugin->flushPluginStateToValueTree();
    juce::ValueTree captured_state = external_plugin->state.createCopy();
    stripAutomationCurves(captured_state);
    return makePluginInstanceState(std::move(captured_state));
}

// Recreates a captured external-plugin memento as a user-visible plugin with its original id.
std::expected<PluginChainSnapshot, PluginHostError> Engine::recreatePluginStatePreservingId(
    const PluginInstanceState& state, std::size_t chain_index)
{
    if (!juce::MessageManager::getInstance()->isThisTheMessageThread())
    {
        return std::unexpected{PluginHostError{PluginHostErrorCode::MessageThreadRequired}};
    }

    auto plugin_state = pluginStateTreeFromMemento(state);
    if (!plugin_state.has_value())
    {
        return std::unexpected{std::move(plugin_state.error())};
    }

    const std::string original_instance_id = pluginInstanceIdFromState(*plugin_state);
    if (original_instance_id.empty())
    {
        return std::unexpected{PluginHostError{
            PluginHostErrorCode::PluginStateRestoreFailed,
            "Captured plugin state does not contain a runtime instance id",
        }};
    }

    if (m_impl->findInstrumentPluginInstance(original_instance_id) != nullptr)
    {
        return std::unexpected{PluginHostError{
            PluginHostErrorCode::PluginStateRestoreFailed,
            "Plugin instance id is already loaded: " + original_instance_id,
        }};
    }

    const std::optional<std::size_t> branch_index = m_impl->audibleBranchIndex();
    if (!branch_index.has_value())
    {
        return std::unexpected{PluginHostError{PluginHostErrorCode::TrackMissing}};
    }

    const std::size_t plugin_count = m_impl->userVisiblePluginCount();
    if (chain_index > plugin_count)
    {
        return std::unexpected{PluginHostError{PluginHostErrorCode::InvalidChainIndex}};
    }

    if (plugin_count >= g_max_signal_chain_plugins)
    {
        return std::unexpected{PluginHostError{
            PluginHostErrorCode::PluginChainLimitExceeded,
            pluginChainLimitExceededMessage(plugin_count),
        }};
    }

    tracktion::Plugin::Ptr inserted_plugin;
    auto mutation_result = m_impl->mutateAndReroutePluginChain(
        [this, branch_index, chain_index, &plugin_state, &inserted_plugin, &original_instance_id]
        -> std::expected<void, PluginChainMutationFailure> {
            // The captured state carries the original instance id, which the cache preserves.
            inserted_plugin =
                m_impl->m_edit->getPluginCache().getOrCreatePluginFor(plugin_state->createCopy());
            auto* const external_plugin =
                inserted_plugin != nullptr
                    ? dynamic_cast<tracktion::ExternalPlugin*>(inserted_plugin.get())
                    : nullptr;
            if (external_plugin == nullptr)
            {
                inserted_plugin = nullptr;
                return std::unexpected{PluginChainMutationFailure{
                    .error =
                        PluginHostError{
                            PluginHostErrorCode::PluginStateRestoreFailed,
                            "Could not recreate captured plugin state",
                        },
                    .reroute_context = "plugin-state recreate rollback failed",
                }};
            }

            const juce::String load_error = external_plugin->getLoadError();
            if (load_error.isNotEmpty())
            {
                inserted_plugin = nullptr;
                return std::unexpected{PluginChainMutationFailure{
                    .error =
                        PluginHostError{
                            PluginHostErrorCode::PluginStateRestoreFailed,
                            load_error.toStdString(),
                        },
                    .reroute_context = "plugin-state recreate load rollback failed",
                }};
            }

            const std::string restored_instance_id =
                inserted_plugin->itemID.toString().toStdString();
            if (restored_instance_id != original_instance_id)
            {
                inserted_plugin = nullptr;
                return std::unexpected{PluginChainMutationFailure{
                    .error =
                        PluginHostError{
                            PluginHostErrorCode::PluginStateRestoreFailed,
                            "Recreated plugin id did not match captured state",
                        },
                    .reroute_context = "plugin-state recreate id rollback failed",
                }};
            }

            if (auto inserted = insertIntoBranch(
                    *m_impl->m_tone_rack, *branch_index, chain_index, inserted_plugin);
                !inserted.has_value())
            {
                inserted_plugin = nullptr;
                return std::unexpected{PluginChainMutationFailure{
                    .error =
                        PluginHostError{
                            PluginHostErrorCode::PluginInsertionFailed,
                            std::move(inserted.error().message),
                        },
                    .reroute_context = "plugin-state recreate rollback failed",
                }};
            }

            return {};
        },
        [this, branch_index, &inserted_plugin] {
            const std::optional<std::size_t> inserted_index =
                m_impl->userVisiblePluginIndexOf(inserted_plugin.get());
            if (inserted_index.has_value() &&
                !removeFromBranch(*m_impl->m_tone_rack, *branch_index, *inserted_index).has_value())
            {
                logInstrumentMonitoringFailure("plugin-state recreate rollback could not unwire");
            }
        },
        "plugin-state recreate route rollback failed");
    if (!mutation_result.has_value())
    {
        return std::unexpected{std::move(mutation_result.error())};
    }

    return m_impl->pluginChainSnapshot();
}

// Restores a captured state chunk into an existing external plugin's live processor.
std::expected<void, PluginHostError> Engine::setPluginState(
    const std::string& instance_id, const PluginInstanceState& state)
{
    if (!juce::MessageManager::getInstance()->isThisTheMessageThread())
    {
        return std::unexpected{PluginHostError{PluginHostErrorCode::MessageThreadRequired}};
    }

    auto plugin_state = pluginStateTreeFromMemento(state);
    if (!plugin_state.has_value())
    {
        return std::unexpected{std::move(plugin_state.error())};
    }

    if (m_impl->instrumentTrack() == nullptr)
    {
        return std::unexpected{PluginHostError{PluginHostErrorCode::TrackMissing}};
    }

    tracktion::Plugin* const plugin = m_impl->findInstrumentPluginInstance(instance_id);
    if (plugin == nullptr || m_impl->isStructuralLiveRigPlugin(plugin))
    {
        return std::unexpected{PluginHostError{
            PluginHostErrorCode::PluginInstanceNotFound,
            "Plugin instance was not found: " + instance_id,
        }};
    }

    auto* const external_plugin = dynamic_cast<tracktion::ExternalPlugin*>(plugin);
    if (external_plugin == nullptr)
    {
        return std::unexpected{PluginHostError{
            PluginHostErrorCode::PluginStateRestoreFailed,
            "Only external plugin states can be restored right now: " +
                plugin->getName().toStdString(),
        }};
    }

    tracktion::TransportControl& transport = m_impl->m_edit->getTransport();
    const bool was_playing = transport.isPlaying();

    {
        const juce::ScopedValueSetter<bool> defer_plugin_undo_capture(
            m_impl->m_plugin_undo_capture_deferred, true);
        external_plugin->restorePluginStateFromValueTree(*plugin_state);
        copyPluginStatePreservingInstanceId(*external_plugin, *plugin_state);
    }

    m_impl->refreshRestoredPluginEditObserver(instance_id, state);

    if (was_playing && !transport.isPlaying())
    {
        RH_LOG_INFO(
            "audio.engine",
            "Resuming transport after plugin state restore instance_id={:?} label_hint={:?}",
            instance_id,
            external_plugin->getName().toStdString());
        transport.play(false);
    }
    m_impl->updateTransportState();

    return {};
}

// Settles or conservatively drops any pending user plugin edits.
void Engine::flushPendingPluginEdits()
{
    if (!juce::MessageManager::getInstance()->isThisTheMessageThread())
    {
        return;
    }

    m_impl->flushPendingPluginEdits();
}

// Reports whether any user plugin edit is waiting for gesture end or debounce.
bool Engine::hasPendingPluginEdits() const
{
    if (!juce::MessageManager::getInstance()->isThisTheMessageThread())
    {
        return false;
    }

    return m_impl->hasPendingPluginEdits();
}

// Stores the observer endpoint driven by Tracktion plugin-edit callbacks.
void Engine::setPluginEditObserver(PluginEditObserver observer)
{
    if (!juce::MessageManager::getInstance()->isThisTheMessageThread())
    {
        return;
    }

    m_impl->m_plugin_edit_observer = std::move(observer);
    m_impl->m_plugin_edit_pending_notified = false;
    m_impl->notifyPluginEditPendingStateChanged();
}

// Stores the observer endpoint driven by Tracktion processor-wide plugin changes.
void Engine::setPluginStateEditObserver(PluginStateEditObserver observer)
{
    if (!juce::MessageManager::getInstance()->isThisTheMessageThread())
    {
        return;
    }

    m_impl->m_plugin_state_edit_observer = std::move(observer);
}

// Stores the app-level endpoint for hosted plugin-window shortcuts.
void Engine::setPluginWindowCommandObserver(PluginWindowCommandObserver observer)
{
    if (!juce::MessageManager::getInstance()->isThisTheMessageThread())
    {
        return;
    }

    m_impl->m_plugin_window_command_observer = std::move(observer);
}

// Opens a plugin editor window through Tracktion's plugin window state.
std::expected<void, PluginHostError> Engine::openPluginWindow(const std::string& instance_id)
{
    if (!juce::MessageManager::getInstance()->isThisTheMessageThread())
    {
        return std::unexpected{PluginHostError{PluginHostErrorCode::MessageThreadRequired}};
    }

    if (m_impl->instrumentTrack() == nullptr)
    {
        return std::unexpected{PluginHostError{PluginHostErrorCode::TrackMissing}};
    }

    tracktion::Plugin* const plugin = m_impl->findInstrumentPluginInstance(instance_id);
    if (plugin == nullptr)
    {
        return std::unexpected{PluginHostError{
            PluginHostErrorCode::PluginInstanceNotFound,
            "Plugin instance was not found: " + instance_id,
        }};
    }

    plugin->showWindowExplicitly();

    // Safe to inspect synchronously: showWindowExplicitly() runs the entire
    // createPluginWindow -> setVisible chain on the message thread (gated above), so
    // isWindowShowing() is authoritative immediately after it returns. This catches real
    // failures such as plugins with no editor (createPluginWindow returns null) or
    // showWindow() bailing out because a modal dialog is in front.
    if (plugin->windowState == nullptr || !plugin->windowState->isWindowShowing())
    {
        return std::unexpected{PluginHostError{
            PluginHostErrorCode::PluginWindowUnavailable,
            "Plugin editor window could not be opened: " + plugin->getName().toStdString(),
        }};
    }

    return {};
}

} // namespace rock_hero::common::audio
