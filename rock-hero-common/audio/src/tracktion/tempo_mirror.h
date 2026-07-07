/*!
\file tempo_mirror.h
\brief One-way projection of Rock Hero's tempo map into a Tracktion tempo sequence.
*/

#pragma once

#include <rock_hero/common/core/timeline/tempo_map.h>
#include <tracktion_engine/tracktion_engine.h>

namespace rock_hero::common::audio
{

/*!
\brief Rewrites a Tracktion tempo sequence to match a Rock Hero tempo map.

Write-only derived output: hosted plugins read host tempo and meter from the edit's sequence, and
Rock Hero never reads the sequence back into project state. Each anchor span becomes one flat
tempo step (curve 1.0) at its quarter-note position, matching the metronome-linear
changes-only-at-anchors model exactly in shape; the edit's beat unit must be pinned to quarter
notes via RockHeroEngineBehavior::lengthOfOneBeatDependsOnTimeSignature(). All writes go through
non-remapping removals and clamp-free CachedValue assignment or insertTempo, so >300 BPM charts
mirror faithfully and nothing in the edit beat-shifts.

Maps with fewer than two anchors define no tempo and leave the sequence untouched.

\param sequence Tracktion tempo sequence to rewrite in place.
\param tempo_map Rock Hero tempo map to mirror.
*/
void mirrorTempoMapIntoSequence(
    tracktion::TempoSequence& sequence, const core::TempoMap& tempo_map);

} // namespace rock_hero::common::audio
