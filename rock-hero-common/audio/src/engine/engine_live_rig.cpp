#include "engine_impl.h"
#include "live_rig/tone_document.h"
#include "shared/audio_path_util.h"
#include "tracktion/live_rig_gain_plugin.h"
#include "tracktion/plugin_state_hygiene.h"

#include <algorithm>
#include <rock_hero/common/core/package/package_id.h>
#include <rock_hero/common/core/shared/json.h>
#include <rock_hero/common/core/shared/juce_path.h>
#include <rock_hero/common/core/shared/logger.h>

namespace rock_hero::common::audio
{

namespace
{

// Runtime-only tone reference for the single passthrough branch a project without authored
// tones receives; never persisted, because capture writes the caller-supplied reference.
constexpr const char* g_placeholder_tone_ref = "placeholder";

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

bool Engine::Impl::isStructuralLiveRigPlugin(const tracktion::Plugin* plugin) const
{
    if (plugin == nullptr)
    {
        return false;
    }
    return plugin->itemID == m_input_gain_plugin_id || plugin->itemID == m_input_meter_plugin_id ||
           plugin->itemID == m_output_gain_plugin_id || plugin->itemID == m_output_meter_plugin_id;
}

LiveRigGainPlugin* Engine::Impl::findStructuralGainPlugin(tracktion::EditItemID plugin_id) const
{
    if (!plugin_id.isValid())
    {
        return nullptr;
    }
    const tracktion::AudioTrack* const instrument_track = instrumentTrack();
    if (instrument_track == nullptr)
    {
        return nullptr;
    }
    for (tracktion::Plugin* const plugin : instrument_track->pluginList)
    {
        if (plugin != nullptr && plugin->itemID == plugin_id)
        {
            return dynamic_cast<LiveRigGainPlugin*>(plugin);
        }
    }
    return nullptr;
}

tracktion::LevelMeterPlugin* Engine::Impl::findLevelMeter(
    tracktion::PluginList& list, tracktion::EditItemID plugin_id)
{
    if (!plugin_id.isValid())
    {
        return nullptr;
    }
    for (tracktion::Plugin* const plugin : list)
    {
        if (plugin != nullptr && plugin->itemID == plugin_id)
        {
            return dynamic_cast<tracktion::LevelMeterPlugin*>(plugin);
        }
    }
    return nullptr;
}

tracktion::LevelMeterPlugin* Engine::Impl::findStructuralMeterPlugin(
    tracktion::EditItemID plugin_id) const
{
    tracktion::AudioTrack* const instrument_track = instrumentTrack();
    if (instrument_track == nullptr)
    {
        return nullptr;
    }
    return findLevelMeter(instrument_track->pluginList, plugin_id);
}

tracktion::LevelMeterPlugin* Engine::Impl::findStructuralMasterMeterPlugin(
    tracktion::EditItemID plugin_id) const
{
    if (m_edit == nullptr)
    {
        return nullptr;
    }
    return findLevelMeter(m_edit->getMasterPluginList(), plugin_id);
}

std::expected<LiveRigGainPlugin*, LiveRigError> Engine::Impl::createLiveRigGainPlugin(
    int insert_index)
{
    tracktion::AudioTrack* const instrument_track = instrumentTrack();
    if (instrument_track == nullptr)
    {
        return std::unexpected{LiveRigError{LiveRigErrorCode::TrackMissing}};
    }
    const tracktion::Plugin::Ptr plugin =
        m_edit->getPluginCache().createNewPlugin(LiveRigGainPlugin::createState());
    if (plugin == nullptr)
    {
        return std::unexpected{LiveRigError{
            LiveRigErrorCode::PluginRestoreFailed,
            "Could not create structural live rig gain plugin",
        }};
    }
    instrument_track->pluginList.insertPlugin(plugin, insert_index, nullptr);
    auto* const live_rig_gain = dynamic_cast<LiveRigGainPlugin*>(plugin.get());
    if (live_rig_gain == nullptr || !instrument_track->pluginList.contains(live_rig_gain))
    {
        return std::unexpected{LiveRigError{
            LiveRigErrorCode::PluginRestoreFailed,
            "Could not insert structural live rig gain plugin",
        }};
    }

    return live_rig_gain;
}

std::expected<tracktion::LevelMeterPlugin*, LiveRigError> Engine::Impl::createLevelMeter(
    tracktion::PluginList& list, int insert_index)
{
    const tracktion::Plugin::Ptr plugin =
        m_edit->getPluginCache().createNewPlugin(tracktion::LevelMeterPlugin::xmlTypeName, {});
    if (plugin == nullptr)
    {
        return std::unexpected{LiveRigError{
            LiveRigErrorCode::PluginRestoreFailed,
            "Could not create structural meter plugin",
        }};
    }
    list.insertPlugin(plugin, insert_index, nullptr);
    auto* const level_meter = dynamic_cast<tracktion::LevelMeterPlugin*>(plugin.get());
    if (level_meter == nullptr || !list.contains(level_meter))
    {
        return std::unexpected{LiveRigError{
            LiveRigErrorCode::PluginRestoreFailed,
            "Could not insert structural meter plugin",
        }};
    }

    return level_meter;
}

std::expected<tracktion::LevelMeterPlugin*, LiveRigError> Engine::Impl::createLevelMeterPlugin(
    int insert_index)
{
    tracktion::AudioTrack* const instrument_track = instrumentTrack();
    if (instrument_track == nullptr)
    {
        return std::unexpected{LiveRigError{LiveRigErrorCode::TrackMissing}};
    }
    return createLevelMeter(instrument_track->pluginList, insert_index);
}

std::expected<tracktion::LevelMeterPlugin*, LiveRigError> Engine::Impl::
    createMasterLevelMeterPlugin()
{
    if (m_edit == nullptr)
    {
        return std::unexpected{LiveRigError{
            LiveRigErrorCode::PluginRestoreFailed,
            "Edit is not available for master meter creation",
        }};
    }
    return createLevelMeter(m_edit->getMasterPluginList(), -1);
}

void Engine::Impl::detachAndClearMeter(MeterReader& reader, tracktion::LevelMeterPlugin* meter)
{
    reader.detach();
    if (meter != nullptr)
    {
        meter->measurer.clear();
    }
}

std::expected<void, LiveRigError> Engine::Impl::validateStructuralLiveRigPlugins() const
{
    const tracktion::AudioTrack* const instrument_track = instrumentTrack();
    if (instrument_track == nullptr)
    {
        return std::unexpected{LiveRigError{LiveRigErrorCode::TrackMissing}};
    }

    const auto* const input_plugin = findStructuralGainPlugin(m_input_gain_plugin_id);
    const auto* const input_meter = findStructuralMeterPlugin(m_input_meter_plugin_id);
    const auto* const output_plugin = findStructuralGainPlugin(m_output_gain_plugin_id);
    const auto* const output_meter = findStructuralMeterPlugin(m_output_meter_plugin_id);
    if (input_plugin == nullptr || input_meter == nullptr || output_plugin == nullptr ||
        output_meter == nullptr)
    {
        return std::unexpected{LiveRigError{
            LiveRigErrorCode::PluginRestoreFailed,
            "Structural live rig plugins are missing",
        }};
    }

    const auto& plugin_list = instrument_track->pluginList.getPlugins();
    if (plugin_list.size() < 4 || plugin_list[0] == nullptr || plugin_list[1] == nullptr)
    {
        return std::unexpected{LiveRigError{
            LiveRigErrorCode::PluginRestoreFailed,
            "Structural live rig plugins are not in fixed slots",
        }};
    }
    if (plugin_list.getLast() == nullptr || plugin_list[plugin_list.size() - 2] == nullptr)
    {
        return std::unexpected{LiveRigError{
            LiveRigErrorCode::PluginRestoreFailed,
            "Structural live rig plugins are not in fixed slots",
        }};
    }

    if (plugin_list[0]->itemID != m_input_gain_plugin_id ||
        plugin_list[1]->itemID != m_input_meter_plugin_id ||
        plugin_list[plugin_list.size() - 2]->itemID != m_output_gain_plugin_id ||
        plugin_list.getLast()->itemID != m_output_meter_plugin_id)
    {
        return std::unexpected{LiveRigError{
            LiveRigErrorCode::PluginRestoreFailed,
            "Structural live rig plugins are not in fixed slots",
        }};
    }

    return {};
}

std::expected<void, LiveRigError> Engine::Impl::createStructuralLiveRigPlugins()
{
    if (instrumentTrack() == nullptr)
    {
        return std::unexpected{LiveRigError{LiveRigErrorCode::TrackMissing}};
    }

    auto created_input_plugin = createLiveRigGainPlugin(0);
    if (!created_input_plugin.has_value())
    {
        return std::unexpected{std::move(created_input_plugin.error())};
    }
    m_input_gain_plugin_id = (*created_input_plugin)->itemID;

    auto created_input_meter = createLevelMeterPlugin(1);
    if (!created_input_meter.has_value())
    {
        return std::unexpected{std::move(created_input_meter.error())};
    }
    m_input_meter_plugin_id = (*created_input_meter)->itemID;

    auto created_output_plugin = createLiveRigGainPlugin(-1);
    if (!created_output_plugin.has_value())
    {
        return std::unexpected{std::move(created_output_plugin.error())};
    }
    m_output_gain_plugin_id = (*created_output_plugin)->itemID;

    auto created_output_meter = createLevelMeterPlugin(-1);
    if (!created_output_meter.has_value())
    {
        return std::unexpected{std::move(created_output_meter.error())};
    }
    m_output_meter_plugin_id = (*created_output_meter)->itemID;

    auto created_master_meter = createMasterLevelMeterPlugin();
    if (!created_master_meter.has_value())
    {
        return std::unexpected{std::move(created_master_meter.error())};
    }
    m_master_meter_plugin_id = (*created_master_meter)->itemID;

    return validateStructuralLiveRigPlugins();
}

std::expected<void, LiveRigError> Engine::Impl::clearUserLiveRigPlugins()
{
    const tracktion::AudioTrack* const instrument_track = instrumentTrack();
    if (instrument_track == nullptr)
    {
        return std::unexpected{LiveRigError{LiveRigErrorCode::TrackMissing}};
    }

    clearPluginEditObservers();
    const tracktion::Plugin::Array plugins = instrument_track->pluginList.getPlugins();
    for (tracktion::Plugin* const plugin : plugins)
    {
        if (plugin != nullptr && !isStructuralLiveRigPlugin(plugin))
        {
            plugin->removeFromParent();
        }
    }
    resetToneRackState();

    return validateStructuralLiveRigPlugins();
}

void Engine::Impl::clearRetainedLiveRigMeterState()
{
    detachAndClearMeter(m_input_meter_reader, findStructuralMeterPlugin(m_input_meter_plugin_id));
    detachAndClearMeter(m_output_meter_reader, findStructuralMeterPlugin(m_output_meter_plugin_id));
    detachAndClearMeter(
        m_master_meter_reader, findStructuralMasterMeterPlugin(m_master_meter_plugin_id));
}

std::expected<void, LiveRigError> Engine::Impl::resetLiveRigProjectState()
{
    auto output_reset = applyGainToPlugin(m_output_gain_plugin_id, Gain{defaultGainDb()});
    if (!output_reset.has_value())
    {
        return std::unexpected{std::move(output_reset.error())};
    }

    clearRetainedLiveRigMeterState();
    return {};
}

Gain Engine::Impl::readGainFromPlugin(tracktion::EditItemID plugin_id) const
{
    const auto* const plugin = findStructuralGainPlugin(plugin_id);
    if (plugin == nullptr)
    {
        return Gain{};
    }
    return plugin->gain();
}

std::expected<void, LiveRigError> Engine::Impl::applyGainToPlugin(
    tracktion::EditItemID plugin_id, Gain gain)
{
    auto* const plugin = findStructuralGainPlugin(plugin_id);
    if (plugin == nullptr)
    {
        return std::unexpected{LiveRigError{
            LiveRigErrorCode::PluginRestoreFailed,
            "Structural live rig gain plugin is missing",
        }};
    }

    plugin->setGain(gain);
    return {};
}

ToneRackBranch* Engine::Impl::audibleToneBranch()
{
    const std::optional<std::size_t> index = audibleBranchIndex();
    if (!m_tone_rack.has_value() || !index.has_value())
    {
        return nullptr;
    }
    return &m_tone_rack->branches[*index];
}

const ToneRackBranch* Engine::Impl::audibleToneBranch() const
{
    const std::optional<std::size_t> index = audibleBranchIndex();
    if (!m_tone_rack.has_value() || !index.has_value())
    {
        return nullptr;
    }
    return &m_tone_rack->branches[*index];
}

std::optional<std::size_t> Engine::Impl::audibleBranchIndex() const
{
    if (!m_tone_rack.has_value())
    {
        return std::nullopt;
    }
    for (std::size_t index = 0; index < m_tone_rack->branches.size(); ++index)
    {
        if (m_tone_rack->branches[index].tone_document_ref == m_audible_tone_ref)
        {
            return index;
        }
    }
    return std::nullopt;
}

std::expected<void, LiveRigError> Engine::Impl::applyAudibleTone(
    const std::string& tone_document_ref)
{
    if (!m_tone_rack.has_value() || !setAudibleBranch(*m_tone_rack, tone_document_ref))
    {
        return std::unexpected{LiveRigError{
            LiveRigErrorCode::InvalidToneDocument,
            "Tone is not loaded in the multi-tone rig: " + tone_document_ref,
        }};
    }

    m_audible_tone_ref = tone_document_ref;
    const std::optional<std::size_t> branch_index = audibleBranchIndex();
    if (branch_index.has_value() && *branch_index < m_branch_output_gains.size())
    {
        return applyGainToPlugin(m_output_gain_plugin_id, m_branch_output_gains[*branch_index]);
    }
    return {};
}

// The rack instance on the track is removed by the non-structural plugin sweep; this releases
// the rack type itself plus the per-branch bookkeeping.
void Engine::Impl::resetToneRackState()
{
    if (m_tone_rack.has_value() && m_edit != nullptr && m_tone_rack->rack_type != nullptr)
    {
        m_edit->getRackList().removeRackType(m_tone_rack->rack_type);
    }
    m_tone_rack.reset();
    m_tone_rack_instance_id = {};
    m_audible_tone_ref.clear();
    m_branch_output_gains.clear();
    m_branch_display_metadata.clear();
}

// Walks the audible branch and pairs each plugin with its retained panel layout; positions past
// the retained metadata fall back to a gapless layout.
LiveRigLoadResult Engine::Impl::audibleToneResult() const
{
    LiveRigLoadResult result;
    const ToneRackBranch* const branch = audibleToneBranch();
    const std::optional<std::size_t> branch_index = audibleBranchIndex();
    if (branch == nullptr || !branch_index.has_value())
    {
        return result;
    }

    static const BranchDisplayMetadata g_empty_metadata{};
    const BranchDisplayMetadata& metadata = *branch_index < m_branch_display_metadata.size()
                                                ? m_branch_display_metadata[*branch_index]
                                                : g_empty_metadata;
    result.plugins.reserve(branch->chain.size());
    for (std::size_t plugin_index = 0; plugin_index < branch->chain.size(); ++plugin_index)
    {
        const auto* const external_plugin =
            dynamic_cast<const tracktion::ExternalPlugin*>(branch->chain[plugin_index].get());
        if (external_plugin == nullptr)
        {
            continue;
        }
        result.plugins.push_back(makePluginChainEntry(*external_plugin, plugin_index));
        if (plugin_index < metadata.block_indices.size())
        {
            result.plugins.back().block_index = metadata.block_indices[plugin_index];
        }
        if (plugin_index < metadata.display_type_overrides.size())
        {
            result.plugins.back().display_type_override =
                metadata.display_type_overrides[plugin_index];
        }
    }
    result.output_gain = *branch_index < m_branch_output_gains.size()
                             ? m_branch_output_gains[*branch_index]
                             : readGainFromPlugin(m_output_gain_plugin_id);
    return result;
}

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

    // Output gain is a per-tone value; remember it on the audible branch so switching away and
    // back restores the edit.
    const std::optional<std::size_t> branch_index = m_impl->audibleBranchIndex();
    if (branch_index.has_value() && *branch_index < m_impl->m_branch_output_gains.size())
    {
        m_impl->m_branch_output_gains[*branch_index] = gain;
    }

    return {};
}

// Switches the audible preloaded tone; only smoothed branch gains move, never the graph.
std::expected<LiveRigLoadResult, LiveRigError> Engine::setAudibleTone(
    const std::string& tone_document_ref)
{
    if (!juce::MessageManager::getInstance()->isThisTheMessageThread())
    {
        return std::unexpected{LiveRigError{LiveRigErrorCode::MessageThreadRequired}};
    }

    if (tone_document_ref != m_impl->m_audible_tone_ref)
    {
        if (auto applied = m_impl->applyAudibleTone(tone_document_ref); !applied.has_value())
        {
            return std::unexpected{std::move(applied.error())};
        }
    }

    return m_impl->audibleToneResult();
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

    const tracktion::AudioTrack* const instrument_track = m_impl->instrumentTrack();
    if (instrument_track == nullptr)
    {
        return std::unexpected{LiveRigError{LiveRigErrorCode::TrackMissing}};
    }

    const std::size_t user_plugin_count = m_impl->userVisiblePluginCount();
    if (user_plugin_count > g_max_signal_chain_plugins)
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

    LiveRigSnapshot snapshot;
    snapshot.output_gain = m_impl->readGainFromPlugin(m_impl->m_output_gain_plugin_id);

    // Every loaded branch persists to its own document: any branch can drift from its file (undo
    // restores plugin state by instance id, plugin windows stay open across audibility switches),
    // so audible-only capture would silently lose non-audible edits. Without a loaded rig nothing
    // has drifted and the documents on disk stay authoritative, so no files are written.
    const std::optional<std::size_t> audible_index = m_impl->audibleBranchIndex();
    const std::optional<ToneRack>& tone_rack = m_impl->m_tone_rack;
    const std::size_t branch_count = tone_rack.has_value() ? tone_rack->branches.size() : 0;
    for (std::size_t branch_index = 0; branch_index < branch_count; ++branch_index)
    {
        if (!tone_rack.has_value())
        {
            break;
        }
        const ToneRackBranch& branch = tone_rack->branches[branch_index];
        // The placeholder branch is runtime-only scaffolding for tone-less rigs and never owns a
        // document on disk.
        if (branch.tone_document_ref == g_placeholder_tone_ref)
        {
            continue;
        }

        const std::filesystem::path tone_document_ref{branch.tone_document_ref};
        if (!isSafeRelativePath(tone_document_ref) ||
            !core::isCanonicalToneDocumentRef(tone_document_ref.generic_string()))
        {
            m_impl->rebuildInstrumentMonitoringGraphBestEffort(
                "tone reference validation rollback failed");
            return std::unexpected{LiveRigError{
                LiveRigErrorCode::InvalidToneDocument,
                "Tone document path must be tones/<uuid>/tone.json: " +
                    tone_document_ref.generic_string()
            }};
        }

        const bool is_audible = audible_index.has_value() && *audible_index == branch_index;
        // The editor's layout vectors describe the audible chain; other branches persist the
        // layout retained from their load or their last audible capture.
        static const Impl::BranchDisplayMetadata g_empty_branch_metadata;
        const Impl::BranchDisplayMetadata& retained_metadata =
            branch_index < m_impl->m_branch_display_metadata.size()
                ? m_impl->m_branch_display_metadata[branch_index]
                : g_empty_branch_metadata;
        const std::vector<std::size_t>& block_layout =
            is_audible ? request.block_indices : retained_metadata.block_indices;
        const std::vector<std::string>& display_layout =
            is_audible ? request.display_type_overrides : retained_metadata.display_type_overrides;
        const std::vector<std::string>& stable_id_layout =
            is_audible ? request.stable_ids : retained_metadata.stable_ids;

        ToneDocument document;
        document.chain.reserve(branch.chain.size());
        if (is_audible)
        {
            snapshot.plugins.reserve(branch.chain.size());
        }
        const std::filesystem::path plugin_state_directory =
            toneDocumentStateDirectory(tone_document_ref);

        std::size_t captured_plugin_index = 0;
        for (const tracktion::Plugin::Ptr& plugin : branch.chain)
        {
            if (plugin == nullptr)
            {
                continue;
            }

            auto* const external_plugin = dynamic_cast<tracktion::ExternalPlugin*>(plugin.get());
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
            // The layout owner may supply fewer entries than the chain holds; fall back to a
            // gapless block and empty tokens as the editor does.
            const std::size_t block_index =
                chain_index < block_layout.size() ? block_layout[chain_index] : chain_index;
            const std::string display_type_override =
                chain_index < display_layout.size() ? display_layout[chain_index] : std::string{};
            const std::string stable_id = chain_index < stable_id_layout.size()
                                              ? stable_id_layout[chain_index]
                                              : std::string{};
            // Without a live instance the flush keeps the previously captured chunk; persisting
            // that is the correct don't-clobber behavior, but it must never pass silently as a
            // fresh capture (source-verified against the vendored flush path).
            if (external_plugin->getAudioPluginInstance() == nullptr)
            {
                RH_LOG_WARNING(
                    "audio.live_rig",
                    "Capturing prior state for plugin without a live instance tone={:?} name={:?}",
                    branch.tone_document_ref,
                    external_plugin->getName().toStdString());
            }
            external_plugin->flushPluginStateToValueTree();
            juce::ValueTree plugin_state = external_plugin->state.createCopy();
            plugin_state.removeProperty(tracktion::IDs::id, nullptr);
            // The seconds curve is a derived cache of the arrangement's musical automation, and
            // the remap flag would let a future edit-tempo write beat-shift that cache behind the
            // mirror's back; neither belongs in persisted plugin state.
            stripAutomationCurves(plugin_state);
            stripTempoRemapFlag(plugin_state);

            const std::filesystem::path plugin_state_ref =
                generatedPluginStatePath(plugin_state_directory, chain_index);
            const std::filesystem::path plugin_state_path =
                request.song_directory / plugin_state_ref;
            const auto plugin_state_xml = makePluginStateXml(plugin_state, plugin_state_path);
            if (!plugin_state_xml.has_value())
            {
                m_impl->rebuildInstrumentMonitoringGraphBestEffort(
                    "plugin-state serialization rollback failed");
                return std::unexpected{plugin_state_xml.error()};
            }

            if (auto write_result = writeTextFile(
                    plugin_state_path,
                    *plugin_state_xml,
                    LiveRigErrorCode::CouldNotWritePluginState);
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
                    .stable_id = stable_id,
                });
            if (is_audible)
            {
                snapshot.plugins.push_back(makePluginChainEntry(*external_plugin, chain_index));
                snapshot.plugins.back().block_index = block_index;
                snapshot.plugins.back().display_type_override = display_type_override;
            }
            ++captured_plugin_index;
        }

        // The audible branch reads the structural output plugin (the live editing surface); other
        // branches persist their retained authored gain, which audible switching keeps in sync.
        document.output_gain = is_audible ? snapshot.output_gain
                                          : (branch_index < m_impl->m_branch_output_gains.size()
                                                 ? m_impl->m_branch_output_gains[branch_index]
                                                 : Gain{defaultGainDb()});

        const std::filesystem::path tone_document_path = request.song_directory / tone_document_ref;
        if (auto write_result = writeToneDocument(tone_document_path, document);
            !write_result.has_value())
        {
            m_impl->rebuildInstrumentMonitoringGraphBestEffort(
                "tone document write rollback failed");
            return std::unexpected{std::move(write_result.error())};
        }

        // The captured layout is now this tone's authoritative panel layout; retain it so
        // switching away and back restores it.
        if (is_audible && branch_index < m_impl->m_branch_display_metadata.size())
        {
            Impl::BranchDisplayMetadata& metadata = m_impl->m_branch_display_metadata[branch_index];
            metadata.block_indices.clear();
            metadata.display_type_overrides.clear();
            metadata.stable_ids.clear();
            for (const PluginRecord& record : document.chain)
            {
                metadata.block_indices.push_back(record.block_index);
                metadata.display_type_overrides.push_back(record.display_type_override);
                metadata.stable_ids.push_back(record.stable_id);
            }
        }
    }

    auto route_result = m_impl->rebuildInstrumentMonitoringGraph();
    if (!route_result.has_value())
    {
        return std::unexpected{liveRigErrorFromLiveInputError(route_result.error())};
    }
    return snapshot;
}

std::expected<void, LiveRigError> Engine::addEmptyToneBranch(const std::string& tone_document_ref)
{
    if (!juce::MessageManager::getInstance()->isThisTheMessageThread())
    {
        return std::unexpected{LiveRigError{LiveRigErrorCode::MessageThreadRequired}};
    }
    if (!m_impl->m_tone_rack.has_value() || m_impl->m_edit == nullptr)
    {
        return std::unexpected{LiveRigError{
            LiveRigErrorCode::InvalidRequest,
            "No loaded rig to add a tone branch to: " + tone_document_ref,
        }};
    }

    // A branch left behind by an earlier add (undo keeps the model authoritative and lets rig
    // branches linger) simply satisfies the request.
    if (std::ranges::any_of(
            m_impl->m_tone_rack->branches, [&tone_document_ref](const ToneRackBranch& branch) {
                return branch.tone_document_ref == tone_document_ref;
            }))
    {
        return {};
    }

    if (auto added =
            audio::addEmptyToneBranch(*m_impl->m_tone_rack, *m_impl->m_edit, tone_document_ref);
        !added.has_value())
    {
        return std::unexpected{std::move(added.error())};
    }

    // The bookkeeping arrays stay parallel to the branches by appending together: unity authored
    // gain and an empty retained layout, exactly what a fresh empty tone document loads as.
    m_impl->m_branch_output_gains.push_back(Gain{defaultGainDb()});
    m_impl->m_branch_display_metadata.push_back(Impl::BranchDisplayMetadata{});
    return {};
}

// Writes a fresh empty tone document (empty chain, unity gain) and returns its package-relative
// reference. Eager persistence lets a subsequent loadLiveRig, which fails on a missing document,
// pick the new reference up as its own passthrough branch.
std::expected<std::string, LiveRigError> Engine::mintEmptyTone(
    const std::filesystem::path& song_directory)
{
    const std::filesystem::path relative_path = generatedToneDocumentPath();
    const ToneDocument document{.chain = {}, .output_gain = Gain{defaultGainDb()}};
    if (auto written = writeToneDocument(song_directory / relative_path, document);
        !written.has_value())
    {
        return std::unexpected{std::move(written.error())};
    }
    return relative_path.generic_string();
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

    auto operation = std::make_unique<LiveRigLoadOperation>();
    if (request.tone_document_refs.empty())
    {
        // A rig must always exist so the panel can build a chain from scratch on a project with
        // no authored tones yet: load one passthrough branch under a placeholder tone. Capture
        // skips placeholder branches, so the placeholder never owns a document on disk.
        request.audible_tone_ref = g_placeholder_tone_ref;
        operation->tones.push_back(
            LiveRigLoadOperation::ToneLoad{
                .tone_document_ref = std::string{g_placeholder_tone_ref},
                .chain = {},
                .display_names = {},
                .output_gain = Gain{defaultGainDb()},
                .loaded_plugins = {},
            });
    }
    else
    {
        if (request.song_directory.empty())
        {
            on_result(std::unexpected{LiveRigError{LiveRigErrorCode::InvalidRequest}});
            return;
        }

        // The audible tone must be one of the loaded set; default to the first tone when unset
        // so legacy single-tone callers stay trivial.
        if (request.audible_tone_ref.empty())
        {
            request.audible_tone_ref = request.tone_document_refs.front();
        }
        if (std::ranges::find(request.tone_document_refs, request.audible_tone_ref) ==
            request.tone_document_refs.end())
        {
            on_result(
                std::unexpected{LiveRigError{
                    LiveRigErrorCode::InvalidRequest,
                    "Audible tone is not part of the requested tone set: " +
                        request.audible_tone_ref,
                }});
            return;
        }

        operation->tones.reserve(request.tone_document_refs.size());
        for (const std::string& tone_ref : request.tone_document_refs)
        {
            // Duplicate references share one branch; callers normally dedupe already.
            const bool already_loading = std::ranges::any_of(
                operation->tones, [&tone_ref](const LiveRigLoadOperation::ToneLoad& tone) {
                    return tone.tone_document_ref == tone_ref;
                });
            if (already_loading)
            {
                continue;
            }

            auto document = readToneDocument(request.song_directory, tone_ref);
            if (!document.has_value())
            {
                on_result(std::unexpected{std::move(document.error())});
                return;
            }

            LiveRigLoadOperation::ToneLoad tone_load{
                .tone_document_ref = tone_ref,
                .chain = std::move(document->chain),
                .display_names = {},
                .output_gain = document->output_gain,
                .loaded_plugins = {},
            };
            tone_load.display_names.reserve(tone_load.chain.size());
            for (std::size_t plugin_index = 0; plugin_index < tone_load.chain.size();
                 ++plugin_index)
            {
                tone_load.display_names.push_back(
                    pluginDisplayName(tone_load.chain[plugin_index].identity, plugin_index));
            }
            operation->total_plugins += tone_load.chain.size();
            tone_load.loaded_plugins.reserve(tone_load.chain.size());
            operation->tones.push_back(std::move(tone_load));
        }
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

    operation->request = std::move(request);
    operation->on_result = std::move(on_result);

    reportLiveRigLoadProgress(operation->request, 0, operation->total_plugins);

    m_impl->m_load_op = std::move(operation);

    // A plugin-less load has no heavy construction to paint around, so it finalizes
    // synchronously, matching the pre-multi-tone empty-load contract callers rely on.
    if (m_impl->m_load_op->total_plugins == 0)
    {
        m_impl->beginNextPluginStep();
        return;
    }

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

    // Advance across tones whose chains are exhausted (or empty) to the next plugin to load.
    while (m_load_op->next_tone < m_load_op->tones.size() &&
           m_load_op->next_plugin >= m_load_op->tones[m_load_op->next_tone].chain.size())
    {
        ++m_load_op->next_tone;
        m_load_op->next_plugin = 0;
    }

    if (m_load_op->next_tone >= m_load_op->tones.size())
    {
        finalizeLiveRigLoad();
        return;
    }

    const LiveRigLoadOperation::ToneLoad& tone = m_load_op->tones[m_load_op->next_tone];
    const std::string& display_name = tone.display_names[m_load_op->next_plugin];

    // "Loading X" before the heavy work so the bar updates to the new plugin name immediately.
    reportLiveRigLoadProgress(
        m_load_op->request,
        m_load_op->completed_plugins,
        m_load_op->total_plugins,
        m_load_op->completed_plugins,
        display_name);

    std::weak_ptr<bool> load_alive_source = m_alive;
    yieldThenContinue([this, load_alive = std::move(load_alive_source)] {
        if (load_alive.expired())
        {
            return;
        }
        executePluginStep();
    });
}

// Assembles the rack from the loaded chains, places its instance on the track, applies the
// audible tone, and delivers the audible tone's chain to the caller.
void Engine::Impl::finalizeLiveRigLoad()
{
    if (m_load_op == nullptr)
    {
        return;
    }

    std::vector<ToneRackBranchRequest> branch_requests;
    branch_requests.reserve(m_load_op->tones.size());
    for (LiveRigLoadOperation::ToneLoad& tone : m_load_op->tones)
    {
        branch_requests.push_back(
            ToneRackBranchRequest{
                .tone_document_ref = tone.tone_document_ref,
                .chain = std::move(tone.loaded_plugins),
            });
    }

    auto built_rack = buildToneRack(*m_edit, branch_requests);
    if (!built_rack.has_value())
    {
        abortLiveRigLoad(std::move(built_rack.error()));
        return;
    }
    // Adopt the rack immediately so every abort path below tears it down through the
    // non-structural sweep plus resetToneRackState().
    m_tone_rack = std::move(*built_rack);
    m_branch_output_gains.clear();
    m_branch_output_gains.reserve(m_load_op->tones.size());
    m_branch_display_metadata.clear();
    m_branch_display_metadata.reserve(m_load_op->tones.size());
    for (const LiveRigLoadOperation::ToneLoad& tone : m_load_op->tones)
    {
        m_branch_output_gains.push_back(tone.output_gain);
        BranchDisplayMetadata metadata;
        metadata.block_indices.reserve(tone.chain.size());
        metadata.display_type_overrides.reserve(tone.chain.size());
        metadata.stable_ids.reserve(tone.chain.size());
        for (const PluginRecord& record : tone.chain)
        {
            metadata.block_indices.push_back(record.block_index);
            metadata.display_type_overrides.push_back(record.display_type_override);
            metadata.stable_ids.push_back(record.stable_id);
        }
        m_branch_display_metadata.push_back(std::move(metadata));
    }

    auto rack_instance = createToneRackInstance(*m_edit, *m_tone_rack->rack_type);
    if (!rack_instance.has_value())
    {
        abortLiveRigLoad(std::move(rack_instance.error()));
        return;
    }

    tracktion::AudioTrack* const instrument_track = instrumentTrack();
    const tracktion::Plugin* const output_gain = findStructuralGainPlugin(m_output_gain_plugin_id);
    const int insert_index = instrument_track != nullptr && output_gain != nullptr
                                 ? instrument_track->pluginList.indexOf(output_gain)
                                 : -1;
    if (insert_index < 0)
    {
        abortLiveRigLoad(
            LiveRigError{
                LiveRigErrorCode::PluginRestoreFailed,
                "Structural live rig output gain plugin is missing",
            });
        return;
    }
    instrument_track->pluginList.insertPlugin(*rack_instance, insert_index, nullptr);
    if (!instrument_track->pluginList.contains(rack_instance->get()))
    {
        abortLiveRigLoad(
            LiveRigError{
                LiveRigErrorCode::PluginRestoreFailed,
                "Could not place the multi-tone rack on the instrument track",
            });
        return;
    }
    m_tone_rack_instance_id = (*rack_instance)->itemID;

    auto structural_valid = validateStructuralLiveRigPlugins();
    if (!structural_valid.has_value())
    {
        abortLiveRigLoad(std::move(structural_valid.error()));
        return;
    }

    auto audible_applied = applyAudibleTone(m_load_op->request.audible_tone_ref);
    if (!audible_applied.has_value())
    {
        abortLiveRigLoad(std::move(audible_applied.error()));
        return;
    }

    auto route_result = rebuildInstrumentMonitoringGraph();
    if (!route_result.has_value())
    {
        abortLiveRigLoad(liveRigErrorFromLiveInputError(route_result.error()));
        return;
    }

    // The caller's result is the audible tone's chain, carrying the authored block layout.
    LiveRigLoadResult result = audibleToneResult();

    // Per-tone plugin identities are only reported here, fresh from the parsed documents; chain
    // mutations can positionally stale the retained metadata, so audible switches never repeat
    // this listing. The editor merges it into its runtime association and owns it afterwards.
    if (m_tone_rack.has_value())
    {
        result.tone_chains.reserve(m_tone_rack->branches.size());
        for (std::size_t branch_index = 0; branch_index < m_tone_rack->branches.size();
             ++branch_index)
        {
            const ToneRackBranch& branch = m_tone_rack->branches[branch_index];
            const std::vector<std::string>* const stable_ids =
                branch_index < m_branch_display_metadata.size()
                    ? &m_branch_display_metadata[branch_index].stable_ids
                    : nullptr;
            LoadedToneChainIdentities identities;
            identities.tone_document_ref = branch.tone_document_ref;
            identities.plugins.reserve(branch.chain.size());
            for (std::size_t plugin_index = 0; plugin_index < branch.chain.size(); ++plugin_index)
            {
                if (branch.chain[plugin_index] == nullptr)
                {
                    continue;
                }
                identities.plugins.push_back(
                    LoadedTonePluginIdentity{
                        .instance_id = branch.chain[plugin_index]->itemID.toString().toStdString(),
                        .stable_id = stable_ids != nullptr && plugin_index < stable_ids->size()
                                         ? (*stable_ids)[plugin_index]
                                         : std::string{},
                    });
            }
            result.tone_chains.push_back(std::move(identities));
        }
    }

    endPluginUndoCaptureDeferral();
    auto operation = std::move(m_load_op);
    operation->on_result(std::move(result));
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

    LiveRigLoadOperation::ToneLoad& tone = m_load_op->tones[m_load_op->next_tone];
    const std::size_t plugin_index = m_load_op->next_plugin;
    const PluginRecord& plugin = tone.chain[plugin_index];
    const std::string& display_name = tone.display_names[plugin_index];

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

    // Plugins restore free-floating through the plugin cache and stay unparented until the
    // finalize step assembles every chain into the multi-tone rack.
    juce::ValueTree state_copy = plugin_state->createCopy();
    // Sidecars written before this build may carry a stale derived curve and the tempo-remap flag;
    // the live curve is rebuilt from the arrangement's musical automation after the load, and the
    // flag must never let Tracktion remap that derived curve.
    stripAutomationCurves(state_copy);
    stripTempoRemapFlag(state_copy);
    tracktion::EditItemID::readOrCreateNewID(*m_edit, state_copy);
    const tracktion::Plugin::Ptr restored_plugin =
        m_edit->getPluginCache().getOrCreatePluginFor(state_copy);
    auto* const external_plugin =
        restored_plugin != nullptr ? dynamic_cast<tracktion::ExternalPlugin*>(restored_plugin.get())
                                   : nullptr;
    if (external_plugin == nullptr)
    {
        abortLiveRigLoad(
            LiveRigError{
                LiveRigErrorCode::PluginRestoreFailed, "Could not restore persisted tone plugin"
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

    tone.loaded_plugins.push_back(restored_plugin);
    m_load_op->next_plugin = plugin_index + 1;
    ++m_load_op->completed_plugins;

    // "Loaded X" advances the bar to N+1/T so the user sees the per-plugin completion the spec
    // calls for, and so a one-plugin chain hits 100% before the overlay clears.
    reportLiveRigLoadProgress(
        m_load_op->request,
        m_load_op->completed_plugins,
        m_load_op->total_plugins,
        m_load_op->completed_plugins - 1,
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
