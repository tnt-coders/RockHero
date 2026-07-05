#include "audio_path_util.h"
#include "engine_impl.h"
#include "plugin_scan.h"
#include "tone_document.h"
#include "tracktion/plugin_move_index.h"

#include <rock_hero/common/core/juce_path.h>

namespace rock_hero::common::audio
{

namespace
{

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

    target_state.removeAllChildren(nullptr);
    const int source_child_count = source_state.getNumChildren();
    for (int index = 0; index < source_child_count; ++index)
    {
        target_state.addChild(source_state.getChild(index).createCopy(), index, nullptr);
    }

    target_plugin.itemID.writeID(target_state, nullptr);
}

} // namespace

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

    tracktion::AudioTrack* const instrument_track = instrumentTrack();
    if (instrument_track == nullptr)
    {
        return std::unexpected{PluginHostError{PluginHostErrorCode::TrackMissing}};
    }

    if (!instrument_track->pluginList.canInsertPlugin())
    {
        return std::unexpected{PluginHostError{PluginHostErrorCode::PluginInsertionFailed}};
    }

    const std::size_t plugin_count = userVisiblePluginCount();
    if (chain_index > plugin_count)
    {
        return std::unexpected{PluginHostError{PluginHostErrorCode::InvalidChainIndex}};
    }

    if (plugin_count >= max_signal_chain_plugins)
    {
        return std::unexpected{PluginHostError{
            PluginHostErrorCode::PluginChainLimitExceeded,
            pluginChainLimitExceededMessage(plugin_count),
        }};
    }

    auto insert_position = tracktionIndexForUserPluginSlot(chain_index);
    if (!insert_position.has_value())
    {
        return std::unexpected{std::move(insert_position.error())};
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
    tracktion::Plugin::Ptr plugin;
    auto mutation_result = mutateAndReroutePluginChain(
        [this, &description, &instrument_track, &insert_position, &plugin]
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

            instrument_track->pluginList.insertPlugin(plugin, *insert_position, nullptr);
            if (instrument_track->pluginList.indexOf(plugin.get()) < 0)
            {
                return std::unexpected{PluginChainMutationFailure{
                    .error = PluginHostError{PluginHostErrorCode::PluginInsertionFailed},
                    .reroute_context = "plugin insertion rollback failed",
                }};
            }

            return {};
        },
        [&plugin] {
            if (plugin != nullptr)
            {
                plugin->deleteFromParent();
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

    tracktion::AudioTrack* const instrument_track = m_impl->instrumentTrack();
    if (instrument_track == nullptr)
    {
        return std::unexpected{PluginHostError{PluginHostErrorCode::TrackMissing}};
    }

    tracktion::Plugin* const plugin = m_impl->findInstrumentPluginInstance(instance_id);
    if (plugin == nullptr || m_impl->isStructuralLiveRigPlugin(plugin))
    {
        return std::unexpected{PluginHostError{
            PluginHostErrorCode::PluginInstanceNotFound,
            "Plugin instance was not found: " + instance_id
        }};
    }

    const std::size_t plugin_count = m_impl->userVisiblePluginCount();
    if (destination_index >= plugin_count)
    {
        return std::unexpected{PluginHostError{PluginHostErrorCode::InvalidChainIndex}};
    }

    const std::optional<std::size_t> current_index = m_impl->userVisiblePluginIndexOf(plugin);
    if (!current_index.has_value())
    {
        return std::unexpected{PluginHostError{
            PluginHostErrorCode::PluginInstanceNotFound,
            "Plugin instance was not found: " + instance_id
        }};
    }

    if (*current_index == destination_index)
    {
        return m_impl->pluginChainSnapshot();
    }

    const int original_tracktion_index = instrument_track->pluginList.indexOf(plugin);
    auto destination_tracktion_index =
        m_impl->tracktionIndexForUserPluginSlot(destination_index, plugin);
    if (!destination_tracktion_index.has_value())
    {
        return std::unexpected{std::move(destination_tracktion_index.error())};
    }

    const tracktion::Plugin::Ptr moved_plugin{plugin};
    const auto rollback_move =
        [this, &instrument_track, &moved_plugin, current_index, original_tracktion_index] {
            auto rollback_tracktion_index =
                m_impl->tracktionIndexForUserPluginSlot(*current_index, moved_plugin.get());
            instrument_track->pluginList.insertPlugin(
                moved_plugin,
                rollback_tracktion_index.has_value() ? *rollback_tracktion_index
                                                     : original_tracktion_index,
                nullptr);
        };

    const int move_insert_index = tracktionInsertionIndexForExistingPluginMove(
        original_tracktion_index, *destination_tracktion_index);
    auto mutation_result = m_impl->mutateAndReroutePluginChain(
        [&instrument_track,
         moved_plugin,
         move_insert_index,
         plugin,
         destination_index,
         &rollback_move,
         this] -> std::expected<void, PluginChainMutationFailure> {
            instrument_track->pluginList.insertPlugin(moved_plugin, move_insert_index, nullptr);
            if (instrument_track->pluginList.indexOf(plugin) < 0 ||
                m_impl->userVisiblePluginIndexOf(plugin) !=
                    std::optional<std::size_t>{destination_index})
            {
                rollback_move();
                return std::unexpected{PluginChainMutationFailure{
                    .error = PluginHostError{PluginHostErrorCode::PluginMoveFailed},
                    .reroute_context = "plugin move rollback failed",
                }};
            }

            return {};
        },
        rollback_move,
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

    tracktion::AudioTrack* const instrument_track = m_impl->instrumentTrack();
    if (instrument_track == nullptr)
    {
        return std::unexpected{PluginHostError{PluginHostErrorCode::TrackMissing}};
    }

    tracktion::Plugin* const plugin = m_impl->findInstrumentPluginInstance(instance_id);
    if (plugin == nullptr || m_impl->isStructuralLiveRigPlugin(plugin))
    {
        return std::unexpected{PluginHostError{
            PluginHostErrorCode::PluginInstanceNotFound,
            "Plugin instance was not found: " + instance_id
        }};
    }

    const int original_tracktion_index = instrument_track->pluginList.indexOf(plugin);
    const tracktion::Plugin::Ptr removed_plugin{plugin};
    auto mutation_result = m_impl->mutateAndReroutePluginChain(
        [&instrument_track, plugin] -> std::expected<void, PluginChainMutationFailure> {
            plugin->removeFromParent();
            if (instrument_track->pluginList.indexOf(plugin) >= 0)
            {
                return std::unexpected{PluginChainMutationFailure{
                    .error = PluginHostError{PluginHostErrorCode::PluginRemovalFailed},
                    .reroute_context = "plugin removal rollback failed",
                }};
            }

            return {};
        },
        [&instrument_track, removed_plugin, original_tracktion_index] {
            instrument_track->pluginList.insertPlugin(
                removed_plugin, original_tracktion_index, nullptr);
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
    return makePluginInstanceState(external_plugin->state.createCopy());
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

    tracktion::AudioTrack* const instrument_track = m_impl->instrumentTrack();
    if (instrument_track == nullptr)
    {
        return std::unexpected{PluginHostError{PluginHostErrorCode::TrackMissing}};
    }

    if (!instrument_track->pluginList.canInsertPlugin())
    {
        return std::unexpected{PluginHostError{PluginHostErrorCode::PluginInsertionFailed}};
    }

    const std::size_t plugin_count = m_impl->userVisiblePluginCount();
    if (chain_index > plugin_count)
    {
        return std::unexpected{PluginHostError{PluginHostErrorCode::InvalidChainIndex}};
    }

    if (plugin_count >= max_signal_chain_plugins)
    {
        return std::unexpected{PluginHostError{
            PluginHostErrorCode::PluginChainLimitExceeded,
            pluginChainLimitExceededMessage(plugin_count),
        }};
    }

    auto insert_position = m_impl->tracktionIndexForUserPluginSlot(chain_index);
    if (!insert_position.has_value())
    {
        return std::unexpected{std::move(insert_position.error())};
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

    tracktion::Plugin::Ptr inserted_plugin;
    auto mutation_result = m_impl->mutateAndReroutePluginChain(
        [&instrument_track,
         &plugin_state,
         &insert_position,
         &inserted_plugin,
         &original_instance_id] -> std::expected<void, PluginChainMutationFailure> {
            auto remove_partial_insert =
                [&instrument_track,
                 &inserted_plugin] -> std::expected<void, PluginChainMutationFailure> {
                if (inserted_plugin != nullptr)
                {
                    const tracktion::Plugin* const inserted_plugin_ptr = inserted_plugin.get();
                    inserted_plugin->deleteFromParent();
                    if (instrument_track->pluginList.indexOf(inserted_plugin_ptr) >= 0)
                    {
                        return std::unexpected{PluginChainMutationFailure{
                            .error =
                                PluginHostError{
                                    PluginHostErrorCode::RollbackContractViolation,
                                    "Could not remove partial recreated plugin",
                                },
                            .reroute_context = "plugin-state recreate rollback failed",
                        }};
                    }
                }
                inserted_plugin = nullptr;
                return {};
            };

            inserted_plugin = instrument_track->pluginList.insertPlugin(
                plugin_state->createCopy(), *insert_position);
            auto* const external_plugin =
                inserted_plugin != nullptr
                    ? dynamic_cast<tracktion::ExternalPlugin*>(inserted_plugin.get())
                    : nullptr;
            if (external_plugin == nullptr)
            {
                if (auto removed = remove_partial_insert(); !removed.has_value())
                {
                    return std::unexpected{std::move(removed.error())};
                }
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
                if (auto removed = remove_partial_insert(); !removed.has_value())
                {
                    return std::unexpected{std::move(removed.error())};
                }
                return std::unexpected{PluginChainMutationFailure{
                    .error =
                        PluginHostError{
                            PluginHostErrorCode::PluginStateRestoreFailed,
                            load_error.toStdString(),
                        },
                    .reroute_context = "plugin-state recreate load rollback failed",
                }};
            }

            if (instrument_track->pluginList.indexOf(inserted_plugin.get()) < 0)
            {
                if (auto removed = remove_partial_insert(); !removed.has_value())
                {
                    return std::unexpected{std::move(removed.error())};
                }
                return std::unexpected{PluginChainMutationFailure{
                    .error = PluginHostError{PluginHostErrorCode::PluginInsertionFailed},
                    .reroute_context = "plugin-state recreate rollback failed",
                }};
            }

            const std::string restored_instance_id =
                inserted_plugin->itemID.toString().toStdString();
            if (restored_instance_id != original_instance_id)
            {
                if (auto removed = remove_partial_insert(); !removed.has_value())
                {
                    return std::unexpected{std::move(removed.error())};
                }
                return std::unexpected{PluginChainMutationFailure{
                    .error =
                        PluginHostError{
                            PluginHostErrorCode::PluginStateRestoreFailed,
                            "Recreated plugin id did not match captured state",
                        },
                    .reroute_context = "plugin-state recreate id rollback failed",
                }};
            }

            return {};
        },
        [&inserted_plugin] {
            if (inserted_plugin != nullptr)
            {
                inserted_plugin->deleteFromParent();
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
