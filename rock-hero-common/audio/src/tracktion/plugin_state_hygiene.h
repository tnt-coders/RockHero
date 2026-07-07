/*!
\file plugin_state_hygiene.h
\brief Keeps derived automation state and stale tempo-remap flags out of persisted plugin trees.
*/

#pragma once

#include <juce_data_structures/juce_data_structures.h>

namespace rock_hero::common::audio
{

/*!
\brief Removes every AUTOMATIONCURVE child from a plugin state tree.

The seconds curve is a derived cache rebuilt from the arrangement's musical automation, so
persisted plugin state (chunk-undo mementos, tone-document sidecars) must never carry it: the two
undo domains stay disjoint, and a stale persisted curve can never shadow the musical truth.

\param plugin_state Plugin state tree to clean in place.
*/
void stripAutomationCurves(juce::ValueTree& plugin_state);

/*!
\brief Removes a persisted remapOnTempoChange flag from a plugin state tree.

Tracktion wrote remapOnTempoChange="1" into every plugin created before RockHero overrode
arePluginsRemappedWhenTempoChanges(). The flag must not ride through sidecars or restores: with it
set, any future edit-tempo mutation would let Tracktion beat-remap RockHero's derived seconds
curves behind the one-way tempo mirror's back.

\param plugin_state Plugin state tree to clean in place.
*/
void stripTempoRemapFlag(juce::ValueTree& plugin_state);

} // namespace rock_hero::common::audio
