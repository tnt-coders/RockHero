#include "engine_impl.h"

namespace rock_hero::common::audio
{

// The master volume plugin is constructed unconditionally by Edit::initialise, so it is never
// null; setVolumeDb converts through the fader curve and drives the automatable parameter with
// per-sample 50 ms smoothing (click-free), touching no graph structure. NOTE (verified): the
// master stage applies only to the default wave output device — both engine tracks route there
// today, so master scales backing playback AND live monitoring; if a track is ever routed to a
// secondary device it would silently escape this fader.
std::expected<void, MixControlsError> Engine::setMasterGain(Gain gain)
{
    tracktion::VolumeAndPanPlugin* const master = m_impl->m_edit->getMasterVolumePlugin().get();
    if (master == nullptr)
    {
        return std::unexpected{MixControlsError{MixControlsErrorCode::MasterUnavailable}};
    }

    master->setVolumeDb(static_cast<float>(clampGain(gain).db));
    return {};
}

// Reports the backend's truthful master value: a fresh edit sits at Tracktion's -3 dB default
// until something sets it, and the port deliberately does not renormalize (the editor shares
// this engine, and silently changing its output loudness is not this surface's call).
Gain Engine::masterGain() const
{
    const tracktion::VolumeAndPanPlugin* const master =
        m_impl->m_edit->getMasterVolumePlugin().get();
    if (master == nullptr)
    {
        return {};
    }

    return Gain{static_cast<double>(master->getVolumeDb())};
}

// The backing track's volume plugin is a separate processing stage from the clip's normalization
// gain (clip gain applies inside the wave node, track volume in the plugin chain), so the two
// compose — dB values add — and this setter can never clobber the loader's normalization.
std::expected<void, MixControlsError> Engine::setBackingGain(Gain gain)
{
    auto* const track = m_impl->backingTrack();
    tracktion::VolumeAndPanPlugin* const volume =
        track != nullptr ? track->getVolumePlugin() : nullptr;
    if (volume == nullptr)
    {
        return std::unexpected{MixControlsError{MixControlsErrorCode::BackingTrackUnavailable}};
    }

    volume->setVolumeDb(static_cast<float>(clampGain(gain).db));
    return {};
}

// Reads the backing track's volume stage; 0 dB is the backend default for track volume plugins.
Gain Engine::backingGain() const
{
    auto* const track = m_impl->backingTrack();
    const tracktion::VolumeAndPanPlugin* const volume =
        track != nullptr ? track->getVolumePlugin() : nullptr;
    if (volume == nullptr)
    {
        return {};
    }

    return Gain{static_cast<double>(volume->getVolumeDb())};
}

} // namespace rock_hero::common::audio
