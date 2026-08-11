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

    // Plugin-delay compensation is poison for a live player: with PDC on, every branch of the
    // multi-tone rack is delayed to the WORST branch's reported latency at all times (the graph
    // aligns parallel paths at sum points). With it off, the player hears only the active
    // branch's real latency; branches misalign only during the 5-10 ms tone crossfade — a brief
    // phase smear, not a timing error. PDC exists to align recorded material in mixes; this
    // product has one live path plus a backing stem, so compensation stays off for BOTH products
    // (latency stance recorded in docs/plans/roadmap/21-game-audio-engine-and-session.md Phase 5
    // and the tone plan's latency amendment). Scoring is unaffected either way: note detection
    // taps the dry input before the rack.
    m_edit->setLatencyCompensationEnabled(false);

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

    // Seed the playback clock's zero boundary so it is meaningful before the first audio block. The
    // rate needs no seeding: the clock stores 1.0 until setPlaybackSpeed() publishes another, and
    // re-stating that default here would be a second place to keep in step by hand.
    m_impl->publishClockBoundary(common::core::TimePosition{});
}

// Stops transport activity and detaches listeners while the Tracktion objects are still alive.
// Destroying them is Impl's job, not this body's -- see the note above the rack reset below.
Engine::~Engine()
{
    // Retire the clock republisher first. Declaration order already destroys it before m_edit;
    // this explicit reset exists so no tick fires MID-teardown, while this body is stopping the
    // transport and unwiring the rack below.
    m_impl->m_clock_republish_timer.reset();

    m_impl->m_alive.reset();

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

    // Removes the rack type from the edit, so this one has to be a call and has to happen here,
    // while the edit is still alive.
    m_impl->resetToneRackState();

    // m_edit and m_engine are deliberately NOT reset here, and neither is any other plugin-holding
    // member. They are declared first in Impl, so ~Impl destroys them last -- after every member
    // that can hold a tracktion::Plugin::Ptr. Resetting them here destroyed the Edit early, and a
    // plugin still held by a later-declared member then ran ~AutomatableEditItem against the freed
    // Edit, which writes into the Edit's item cache. m_replace_op is the reachable case: close the
    // editor while a tone-chain swap is mid-flight and its candidate plugins outlive the Edit.
    // Declaration order is the single authority for teardown order; the hand-maintained list that
    // used to live here was a second one, and it was missing m_replace_op.
}

// Creates an IThumbnail wrapper without exposing Tracktion types through public UI-facing headers.
std::unique_ptr<IThumbnail> Engine::createThumbnail(juce::Component& owner)
{
    return std::make_unique<TracktionThumbnail>(*m_impl->m_engine, owner);
}

} // namespace rock_hero::common::audio
