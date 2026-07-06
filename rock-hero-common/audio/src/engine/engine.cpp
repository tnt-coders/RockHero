#include "engine/engine.h"

#include "engine_impl.h"
#include "shared/audio_path_util.h"
#include "tracktion/engine_behaviors.h"
#include "tracktion/plugin_dirty_tracking.h"
#include "tracktion/tracktion_thumbnail.h"

#include <memory>
#include <rock_hero/common/core/shared/application_identity.h>

namespace rock_hero::common::audio
{

void Engine::Impl::createEdit()
{
    m_edit = tracktion::Edit::createSingleTrackEdit(*m_engine);
    auto audio_tracks = tracktion::getAudioTracks(*m_edit);
    tracktion::AudioTrack* const backing_track = audio_tracks.getFirst();

    if (backing_track != nullptr)
    {
        backing_track->setName("Backing");
        m_backing_track_id = backing_track->itemID;
    }
    else
    {
        logInstrumentMonitoringFailure("backing track was not created");
    }

    // Structural live-rig plugins are managed explicitly rather than relying on Tracktion's
    // default plugin insertion.
    constexpr bool add_default_plugins = false;
    const tracktion::AudioTrack::Ptr instrument_track = m_edit->insertNewAudioTrack(
        tracktion::TrackInsertPoint::getEndOfTracks(*m_edit), nullptr, add_default_plugins);

    if (instrument_track != nullptr)
    {
        instrument_track->setName("Instrument");
        m_instrument_track_id = instrument_track->itemID;
        if (auto structural_created = createStructuralLiveRigPlugins();
            !structural_created.has_value())
        {
            logInstrumentMonitoringFailure(toJuceString(structural_created.error().message));
        }
    }
    else
    {
        logInstrumentMonitoringFailure("instrument track was not created");
    }

    m_edit->playInStopEnabled = true;
}

void Engine::Impl::changeListenerCallback(juce::ChangeBroadcaster* source)
{
    if (source == &m_engine->getDeviceManager().deviceManager)
    {
        scheduleAudioDeviceConfigurationRefresh();
        return;
    }
    updateTransportState();
}

tracktion::AudioTrack* Engine::Impl::backingTrack() const
{
    return tracktion::findAudioTrackForID(*m_edit, m_backing_track_id);
}

tracktion::AudioTrack* Engine::Impl::instrumentTrack() const
{
    return tracktion::findAudioTrackForID(*m_edit, m_instrument_track_id);
}

// Creates the Tracktion engine and a minimal two-track edit for playback and instrument monitoring.
Engine::Engine()
    : m_impl(std::make_unique<Impl>())
{
    // Tracktion uses the engine application name as its property-storage folder.
    Impl* const impl = m_impl.get();
    m_impl->m_engine = std::make_unique<tracktion::Engine>(
        toJuceString(core::applicationDataFolderName()),
        std::make_unique<RockHeroUIBehavior>(
            [impl](PluginWindowCommand command) { impl->dispatchPluginWindowCommand(command); }),
        std::make_unique<RockHeroEngineBehavior>());
    m_impl->m_engine->getPluginManager().setUsesSeparateProcessForScanning(true);

    // createSingleTrackEdit already provides one AudioTrack ready for media.
    m_impl->createEdit();

    // Start with one instrument input and stereo output; the dialog can reconfigure either at
    // runtime.
    m_impl->m_engine->getDeviceManager().initialise(1, 2);
    m_impl->rebuildInstrumentMonitoringGraphBestEffort("initial monitoring route setup failed");

    auto& device_manager = m_impl->m_engine->getDeviceManager().deviceManager;
    device_manager.addChangeListener(m_impl.get());

    // TransportControl derives from juce::ChangeBroadcaster and notifies on any transport
    // state change; Impl::changeListenerCallback filters to genuine play/pause transitions.
    m_impl->m_edit->getTransport().addChangeListener(m_impl.get());

    // Tracktion mirrors current playhead position into this public ValueTree property from its
    // transport loop. Listening here keeps the adapter event-driven from the UI perspective.
    m_impl->m_edit->getTransport().state.addListener(m_impl.get());

    // Seeds the project-owned state from the freshly created empty edit.
    m_impl->updateTransportState();
}

// Stops transport activity before destroying Tracktion objects in dependency order.
Engine::~Engine()
{
    m_impl->m_alive.reset();
    m_impl->m_load_op.reset();

    if (m_impl->m_engine)
    {
        auto& device_manager = m_impl->m_engine->getDeviceManager().deviceManager;
        device_manager.removeChangeListener(m_impl.get());
    }

    if (m_impl->m_edit)
    {
        m_impl->m_edit->getTransport().state.removeListener(m_impl.get());
        m_impl->m_edit->getTransport().removeChangeListener(m_impl.get());
        m_impl->stopTransportAndReleaseContext();
    }

    // The rack state holds reference-counted plugins owned by the edit; release it while the
    // edit is still alive or the deferred plugin destructors dereference a destroyed Edit.
    m_impl->resetToneRackState();
    m_impl->m_edit.reset();
    m_impl->m_engine.reset();
}

// Creates an IThumbnail wrapper without exposing Tracktion types through public UI-facing headers.
std::unique_ptr<IThumbnail> Engine::createThumbnail(juce::Component& owner)
{
    return std::make_unique<TracktionThumbnail>(*m_impl->m_engine, owner);
}

} // namespace rock_hero::common::audio
