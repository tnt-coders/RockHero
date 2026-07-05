#include "audio_path_util.h"
#include "engine_impl.h"
#include "tone_document.h"
#include "tracktion/live_rig_gain_plugin.h"

#include <rock_hero/common/core/json.h>
#include <rock_hero/common/core/juce_path.h>
#include <rock_hero/common/core/package_id.h>

namespace rock_hero::common::audio
{

namespace
{

// Maps monitoring rebuild failures into live-rig mutation errors.
[[nodiscard]] LiveRigError liveRigErrorFromLiveInputError(const LiveInputError& error)
{
    switch (error.code)
    {
        case LiveInputErrorCode::MessageThreadRequired:
        {
            return LiveRigError{LiveRigErrorCode::MessageThreadRequired, error.message};
        }
        case LiveInputErrorCode::TrackMissing:
        {
            return LiveRigError{LiveRigErrorCode::TrackMissing, error.message};
        }
        default:
        {
            return LiveRigError{LiveRigErrorCode::MonitoringRouteFailed, error.message};
        }
    }
}

// Sends progress only when the caller provided a progress callback.
void reportLiveRigLoadProgress(
    const LiveRigLoadRequest& request, std::size_t completed_plugins, std::size_t total_plugins,
    std::size_t active_plugin_index = 0, const std::string& active_plugin_name = {})
{
    if (!request.progress_callback)
    {
        return;
    }

    request.progress_callback(
        LiveRigLoadProgress{
            .completed_plugins = completed_plugins,
            .total_plugins = total_plugins,
            .active_plugin_index = active_plugin_index,
            .active_plugin_name = active_plugin_name,
        });
}

} // namespace

// Clears the instrument plugin chain without touching the active backing arrangement.
std::expected<void, LiveRigError> Engine::clearLiveRig()
{
    if (!juce::MessageManager::getInstance()->isThisTheMessageThread())
    {
        return std::unexpected{LiveRigError{LiveRigErrorCode::MessageThreadRequired}};
    }

    // Clear also cancels cooperative restore steps queued by loadLiveRig(); otherwise stale
    // continuations could rebuild the chain after the editor has closed the project.
    m_impl->m_load_op.reset();
    m_impl->clearPluginUndoCaptureDeferral();

    if (m_impl->instrumentTrack() == nullptr)
    {
        return std::unexpected{LiveRigError{LiveRigErrorCode::TrackMissing}};
    }

    m_impl->stopTransportAndReleaseContext();
    auto cleared = m_impl->clearUserLiveRigPlugins();
    if (!cleared.has_value())
    {
        m_impl->endPluginUndoCaptureDeferral();
        return std::unexpected{std::move(cleared.error())};
    }

    auto reset = m_impl->resetLiveRigProjectState();
    if (!reset.has_value())
    {
        m_impl->endPluginUndoCaptureDeferral();
        return std::unexpected{std::move(reset.error())};
    }

    auto route_result = m_impl->rebuildInstrumentMonitoringGraph();
    if (!route_result.has_value())
    {
        m_impl->endPluginUndoCaptureDeferral();
        return std::unexpected{liveRigErrorFromLiveInputError(route_result.error())};
    }
    m_impl->endPluginUndoCaptureDeferral();
    return {};
}

// Reads the current output gain from the structural live-rig gain plugin.
Gain Engine::outputGain() const
{
    return m_impl->readGainFromPlugin(m_impl->m_output_gain_plugin_id);
}

// Sets the output gain on the structural live-rig gain plugin after the signal chain.
std::expected<void, LiveRigError> Engine::setOutputGain(Gain gain)
{
    if (!juce::MessageManager::getInstance()->isThisTheMessageThread())
    {
        return std::unexpected{LiveRigError{LiveRigErrorCode::MessageThreadRequired}};
    }

    gain = clampGain(gain);
    auto applied = m_impl->applyGainToPlugin(m_impl->m_output_gain_plugin_id, gain);
    if (!applied.has_value())
    {
        return std::unexpected{std::move(applied.error())};
    }

    return {};
}

// Captures the current Tracktion live rig chain into a tone document plus plugin-state sidecars.
std::expected<LiveRigSnapshot, LiveRigError> Engine::captureActiveRig(
    const LiveRigCaptureRequest& request)
{
    if (!juce::MessageManager::getInstance()->isThisTheMessageThread())
    {
        return std::unexpected{LiveRigError{LiveRigErrorCode::MessageThreadRequired}};
    }

    if (request.song_directory.empty() || !core::isCanonicalPackageId(request.arrangement_id))
    {
        return std::unexpected{LiveRigError{LiveRigErrorCode::InvalidRequest}};
    }

    const std::filesystem::path tone_document_ref =
        request.existing_tone_document_ref.empty()
            ? generatedToneDocumentPath()
            : std::filesystem::path{request.existing_tone_document_ref};
    if (!isSafeRelativePath(tone_document_ref) ||
        !core::isCanonicalToneDocumentRef(tone_document_ref.generic_string()))
    {
        return std::unexpected{LiveRigError{
            LiveRigErrorCode::InvalidToneDocument,
            "Tone document path must be tones/<uuid>/tone.json: " +
                tone_document_ref.generic_string()
        }};
    }

    const tracktion::AudioTrack* const instrument_track = m_impl->instrumentTrack();
    if (instrument_track == nullptr)
    {
        return std::unexpected{LiveRigError{LiveRigErrorCode::TrackMissing}};
    }

    const std::size_t user_plugin_count = m_impl->userVisiblePluginCount();
    if (user_plugin_count > max_signal_chain_plugins)
    {
        return std::unexpected{LiveRigError{
            LiveRigErrorCode::PluginChainLimitExceeded,
            pluginChainLimitExceededMessage(user_plugin_count),
        }};
    }

    // Defer plugin-undo capture for the whole save: the teardown and rebuild below make the
    // plugins re-announce their parameter state, which must not be recorded as undo entries. The
    // guard clears the deferral and reinstalls the plugin edit observers on every exit path,
    // absorbing the post-rebuild re-announce.
    const Impl::ScopedPluginUndoCaptureDeferral capture_deferral{*m_impl, true};

    m_impl->stopTransportAndReleaseContext();

    ToneDocument document;
    LiveRigSnapshot snapshot{
        .tone_document_ref = tone_document_ref.generic_string(),
        .plugins = {},
    };
    const tracktion::Plugin::Array plugins = instrument_track->pluginList.getPlugins();
    document.chain.reserve(static_cast<std::size_t>(plugins.size()));
    snapshot.plugins.reserve(static_cast<std::size_t>(plugins.size()));
    const std::filesystem::path plugin_state_directory =
        toneDocumentStateDirectory(tone_document_ref);

    std::size_t captured_plugin_index = 0;
    for (tracktion::Plugin* const plugin : plugins)
    {
        if (plugin == nullptr)
        {
            continue;
        }

        // Structural plugins are captured as authored tone state or runtime-only infrastructure,
        // not as chain entries.
        if (m_impl->isStructuralLiveRigPlugin(plugin))
        {
            continue;
        }

        auto* const external_plugin = dynamic_cast<tracktion::ExternalPlugin*>(plugin);
        if (external_plugin == nullptr)
        {
            m_impl->rebuildInstrumentMonitoringGraphBestEffort(
                "unsupported plugin capture rollback failed");
            return std::unexpected{LiveRigError{
                LiveRigErrorCode::UnsupportedPlugin,
                "Only external plugins can be captured right now: " +
                    plugin->getName().toStdString()
            }};
        }

        const std::size_t chain_index = captured_plugin_index;
        // The editor owns the visual placement; fall back to a gapless block when none is supplied.
        const std::size_t block_index = chain_index < request.block_indices.size()
                                            ? request.block_indices[chain_index]
                                            : chain_index;
        const std::string display_type_override =
            chain_index < request.display_type_overrides.size()
                ? request.display_type_overrides[chain_index]
                : std::string{};
        external_plugin->flushPluginStateToValueTree();
        juce::ValueTree plugin_state = external_plugin->state.createCopy();
        plugin_state.removeProperty(tracktion::IDs::id, nullptr);

        const std::filesystem::path plugin_state_ref =
            generatedPluginStatePath(plugin_state_directory, chain_index);
        const std::filesystem::path plugin_state_path = request.song_directory / plugin_state_ref;
        const auto plugin_state_xml = makePluginStateXml(plugin_state, plugin_state_path);
        if (!plugin_state_xml.has_value())
        {
            m_impl->rebuildInstrumentMonitoringGraphBestEffort(
                "plugin-state serialization rollback failed");
            return std::unexpected{plugin_state_xml.error()};
        }

        if (auto write_result = writeTextFile(
                plugin_state_path, *plugin_state_xml, LiveRigErrorCode::CouldNotWritePluginState);
            !write_result.has_value())
        {
            m_impl->rebuildInstrumentMonitoringGraphBestEffort(
                "plugin-state write rollback failed");
            return std::unexpected{std::move(write_result.error())};
        }

        document.chain.push_back(
            PluginRecord{
                .id = "plugin-" + std::to_string(chain_index + 1),
                .identity = makePluginIdentity(external_plugin->desc),
                .tracktion_state_ref = plugin_state_ref.generic_string(),
                .block_index = block_index,
                .display_type_override = display_type_override,
            });
        snapshot.plugins.push_back(makePluginChainEntry(*external_plugin, chain_index));
        snapshot.plugins.back().block_index = block_index;
        snapshot.plugins.back().display_type_override = display_type_override;
        ++captured_plugin_index;
    }

    // Read authored output gain from the structural plugin for persistence and snapshot.
    const Gain captured_output_gain = m_impl->readGainFromPlugin(m_impl->m_output_gain_plugin_id);
    document.output_gain = captured_output_gain;
    snapshot.output_gain = captured_output_gain;

    const std::filesystem::path tone_document_path = request.song_directory / tone_document_ref;
    if (auto write_result = writeToneDocument(tone_document_path, document);
        !write_result.has_value())
    {
        m_impl->rebuildInstrumentMonitoringGraphBestEffort("tone document write rollback failed");
        return std::unexpected{std::move(write_result.error())};
    }

    auto route_result = m_impl->rebuildInstrumentMonitoringGraph();
    if (!route_result.has_value())
    {
        return std::unexpected{liveRigErrorFromLiveInputError(route_result.error())};
    }
    return snapshot;
}

// Kicks off the cooperative async live rig load: validates the request, reads the tone document
// up front, clears the existing chain, and posts the first plugin step on the message loop so
// the busy overlay has a chance to paint before plugin construction starts.
void Engine::loadLiveRig(LiveRigLoadRequest request, LiveRigLoadResultCallback on_result)
{
    if (!on_result)
    {
        return;
    }

    if (!juce::MessageManager::getInstance()->isThisTheMessageThread())
    {
        on_result(std::unexpected{LiveRigError{LiveRigErrorCode::MessageThreadRequired}});
        return;
    }

    if (request.tone_document_ref.empty())
    {
        auto clear_result = clearLiveRig();
        if (!clear_result.has_value())
        {
            on_result(std::unexpected{std::move(clear_result.error())});
            return;
        }

        const Gain default_gain{defaultGainDb()};
        auto output_result = setOutputGain(default_gain);
        if (!output_result.has_value())
        {
            on_result(std::unexpected{std::move(output_result.error())});
            return;
        }

        on_result(
            LiveRigLoadResult{
                .plugins = {},
                .output_gain = default_gain,
            });
        return;
    }

    if (request.song_directory.empty())
    {
        on_result(std::unexpected{LiveRigError{LiveRigErrorCode::InvalidRequest}});
        return;
    }

    auto document = readToneDocument(request.song_directory, request.tone_document_ref);
    if (!document.has_value())
    {
        on_result(std::unexpected{std::move(document.error())});
        return;
    }

    if (m_impl->instrumentTrack() == nullptr)
    {
        on_result(std::unexpected{LiveRigError{LiveRigErrorCode::TrackMissing}});
        return;
    }

    m_impl->beginPluginUndoCaptureDeferral();
    m_impl->stopTransportAndReleaseContext();
    auto cleared = m_impl->clearUserLiveRigPlugins();
    if (!cleared.has_value())
    {
        m_impl->endPluginUndoCaptureDeferral();
        on_result(std::unexpected{std::move(cleared.error())});
        return;
    }

    auto reset = m_impl->resetLiveRigProjectState();
    if (!reset.has_value())
    {
        m_impl->endPluginUndoCaptureDeferral();
        on_result(std::unexpected{std::move(reset.error())});
        return;
    }

    auto operation = std::make_unique<LiveRigLoadOperation>();
    operation->request = std::move(request);
    operation->chain = std::move(document->chain);
    operation->output_gain = document->output_gain;
    operation->display_names.reserve(operation->chain.size());
    for (std::size_t plugin_index = 0; plugin_index < operation->chain.size(); ++plugin_index)
    {
        operation->display_names.push_back(
            pluginDisplayName(operation->chain[plugin_index].identity, plugin_index));
    }
    operation->result.plugins.reserve(operation->chain.size());
    operation->on_result = std::move(on_result);

    reportLiveRigLoadProgress(operation->request, 0, operation->chain.size());

    m_impl->m_load_op = std::move(operation);

    // Yield through the caller-provided paint fence so the initial "Loading live rig..." state
    // actually paints before plugin construction starts.
    std::weak_ptr<bool> load_alive_source = m_impl->m_alive;
    m_impl->yieldThenContinue([this, load_alive = std::move(load_alive_source)] {
        if (load_alive.expired())
        {
            return;
        }
        m_impl->beginNextPluginStep();
    });
}

// Starts the next plugin's step: completes the load if no plugins remain, otherwise reports
// "Loading X" progress and yields so the busy overlay actually paints the new state before the
// heavy plugin construction blocks the message thread.
void Engine::Impl::beginNextPluginStep()
{
    if (m_load_op == nullptr)
    {
        return;
    }

    const std::size_t total_plugins = m_load_op->chain.size();
    if (m_load_op->next_index >= total_plugins)
    {
        auto structural_valid = validateStructuralLiveRigPlugins();
        if (!structural_valid.has_value())
        {
            abortLiveRigLoad(std::move(structural_valid.error()));
            return;
        }

        auto output_gain_applied =
            applyGainToPlugin(m_output_gain_plugin_id, m_load_op->output_gain);
        if (!output_gain_applied.has_value())
        {
            abortLiveRigLoad(std::move(output_gain_applied.error()));
            return;
        }

        auto route_result = rebuildInstrumentMonitoringGraph();
        if (!route_result.has_value())
        {
            abortLiveRigLoad(liveRigErrorFromLiveInputError(route_result.error()));
            return;
        }

        endPluginUndoCaptureDeferral();
        auto operation = std::move(m_load_op);
        operation->result.output_gain = operation->output_gain;
        operation->on_result(std::move(operation->result));
        return;
    }

    const std::size_t plugin_index = m_load_op->next_index;
    const std::string& display_name = m_load_op->display_names[plugin_index];

    // "Loading X" before the heavy work so the bar updates to the new plugin name immediately.
    reportLiveRigLoadProgress(
        m_load_op->request, plugin_index, total_plugins, plugin_index, display_name);

    std::weak_ptr<bool> load_alive_source = m_alive;
    yieldThenContinue([this, load_alive = std::move(load_alive_source)] {
        if (load_alive.expired())
        {
            return;
        }
        executePluginStep();
    });
}

// Performs the heavy work for the current plugin (scan-if-needed, read state, insert,
// error-check), reports "Loaded X" progress, then yields again so the completion of that plugin
// is visible before the next plugin's "Loading" message replaces it.
void Engine::Impl::executePluginStep()
{
    if (m_load_op == nullptr)
    {
        return;
    }

    const std::size_t plugin_index = m_load_op->next_index;
    const std::size_t total_plugins = m_load_op->chain.size();
    const PluginRecord& plugin = m_load_op->chain[plugin_index];
    const std::string& display_name = m_load_op->display_names[plugin_index];

    auto plugin_known = ensureKnownPluginForIdentity(plugin.identity);
    if (!plugin_known.has_value())
    {
        abortLiveRigLoad(std::move(plugin_known.error()));
        return;
    }

    const auto plugin_state_path =
        resolvePackageFile(m_load_op->request.song_directory, plugin.tracktion_state_ref);
    if (!plugin_state_path.has_value())
    {
        abortLiveRigLoad(
            LiveRigError{
                LiveRigErrorCode::MissingPluginState,
                "Tone plugin state is missing or unsafe: " + plugin.tracktion_state_ref
            });
        return;
    }

    auto plugin_state = readPluginStateTree(*plugin_state_path);
    if (!plugin_state.has_value())
    {
        abortLiveRigLoad(std::move(plugin_state.error()));
        return;
    }

    tracktion::AudioTrack* const instrument_track = instrumentTrack();
    if (instrument_track == nullptr)
    {
        abortLiveRigLoad(LiveRigError{LiveRigErrorCode::TrackMissing});
        return;
    }

    const tracktion::Plugin* const output_gain = findStructuralGainPlugin(m_output_gain_plugin_id);
    const int insert_index =
        output_gain != nullptr ? instrument_track->pluginList.indexOf(output_gain) : -1;
    if (insert_index < 0)
    {
        abortLiveRigLoad(
            LiveRigError{
                LiveRigErrorCode::PluginRestoreFailed,
                "Structural live rig output gain plugin is missing",
            });
        return;
    }

    const tracktion::Plugin::Ptr inserted_plugin =
        instrument_track->pluginList.insertPlugin(*plugin_state, insert_index);
    auto* const external_plugin =
        inserted_plugin != nullptr ? dynamic_cast<tracktion::ExternalPlugin*>(inserted_plugin.get())
                                   : nullptr;
    if (external_plugin == nullptr)
    {
        abortLiveRigLoad(
            LiveRigError{
                LiveRigErrorCode::PluginRestoreFailed, "Could not insert persisted tone plugin"
            });
        return;
    }

    const juce::String load_error = external_plugin->getLoadError();
    if (load_error.isNotEmpty())
    {
        abortLiveRigLoad(
            LiveRigError{LiveRigErrorCode::PluginRestoreFailed, load_error.toStdString()});
        return;
    }

    m_load_op->result.plugins.push_back(
        makePluginChainEntry(*external_plugin, m_load_op->result.plugins.size()));
    // The runtime chain has no gap concept, so carry the authored block placement from the parsed
    // tone document into the restored chain entry.
    m_load_op->result.plugins.back().block_index = m_load_op->chain[plugin_index].block_index;
    m_load_op->result.plugins.back().display_type_override =
        m_load_op->chain[plugin_index].display_type_override;
    m_load_op->next_index = plugin_index + 1;

    // "Loaded X" advances the bar to N+1/T so the user sees the per-plugin completion the spec
    // calls for, and so a one-plugin chain hits 100% before the overlay clears.
    reportLiveRigLoadProgress(
        m_load_op->request,
        m_load_op->result.plugins.size(),
        total_plugins,
        plugin_index,
        display_name);

    std::weak_ptr<bool> load_alive_source = m_alive;
    yieldThenContinue([this, load_alive = std::move(load_alive_source)] {
        if (load_alive.expired())
        {
            return;
        }
        beginNextPluginStep();
    });
}

// Routes the continuation through the caller's yield callback when one is provided so each step
// waits for a real paint cycle before resuming. Falls back to plain callAsync so the loop still
// advances when the caller has not supplied a paint fence (e.g. headless tests).
void Engine::Impl::yieldThenContinue(std::function<void()> next)
{
    if (!next)
    {
        return;
    }

    if (m_load_op != nullptr && m_load_op->request.yield_callback)
    {
        m_load_op->request.yield_callback(std::move(next));
        return;
    }

    juce::MessageManager::callAsync(std::move(next));
}

// Tears down a partially loaded chain and delivers the failure to the original caller.
void Engine::Impl::abortLiveRigLoad(LiveRigError error)
{
    if (m_load_op == nullptr)
    {
        return;
    }

    auto operation = std::move(m_load_op);
    if (instrumentTrack() != nullptr)
    {
        auto cleared = clearUserLiveRigPlugins();
        if (!cleared.has_value())
        {
            logInstrumentMonitoringFailure(toJuceString(cleared.error().message));
        }
        auto reset = resetLiveRigProjectState();
        if (!reset.has_value())
        {
            logInstrumentMonitoringFailure(toJuceString(reset.error().message));
        }
    }
    rebuildInstrumentMonitoringGraphBestEffort("live rig load abort rollback failed");
    endPluginUndoCaptureDeferral();
    operation->on_result(std::unexpected{std::move(error)});
}

} // namespace rock_hero::common::audio
