# Tracktion Device-Change Invalidation Plan

## Problem

Tracktion currently treats an accepted MIDI device-list update as active playback graph
invalidation. `DeviceManager::applyNewMidiDeviceList()` updates the engine-level MIDI device lists,
then unconditionally calls `clearAllContextDevices()` and `reloadAllContextDevices()`.

That is too broad. Device discovery is not the same as graph invalidation. A newly discovered MIDI
device, or an unused removed MIDI device, should update Tracktion's device registry and notify UI
listeners without touching an active audio playback graph.

The visible Rock Hero symptom is a small playback dropout during startup MIDI scanning and MIDI
hotplug. Earlier transport-resume patches kept playback from stopping, but they still rebuilt the
graph, so a small audio interruption remained unavoidable.

## Target Behavior

Device-list changes should be classified before active contexts are touched.

No playback graph rebuild:

- A new MIDI device appears and no active edit routes to it.
- An unused MIDI device disappears.
- A new audio device appears and is not selected or routed into an active edit.
- An unused audio device disappears.

Playback graph rebuild or explicit interruption:

- The active audio output disappears.
- A default output changes while an active edit routes to the default output.
- An enabled output channel used by the graph changes.
- A monitored or recording input used by the active edit changes.
- Recording is active and an armed input changes.
- A MIDI output used for track output, click, MIDI clock, MTC, or controller clock changes.
- A MIDI input used for live monitoring, recording, or insert return changes.

In the rebuild cases, a pause or stop is acceptable because the graph's actual route changed. The
bug is only that unrelated device-list churn currently forces the same destructive path.

## Proposed Design

Keep the Tracktion patch small by adding an invalidation gate around the existing reload calls
instead of rewriting graph construction.

1. Snapshot affected device IDs/names before swapping device lists.

   For MIDI scans, compute which MIDI inputs and outputs were added, removed, had enablement
   changed, or became the default MIDI route.

2. Ask each active `EditPlaybackContext` whether the affected devices matter to its current graph.

   The check should be conservative but route-aware:

   - If the transport is recording, treat affected input changes as graph-affecting.
   - If any track output resolves to an affected MIDI output, rebuild.
   - If click, clock, MTC, or controller-clock output resolves to an affected MIDI output, rebuild.
   - If a live monitored, armed, or insert-return MIDI input resolves to an affected MIDI input,
     rebuild.
   - Otherwise, do not rebuild that context.

3. For unaffected contexts, do not call `ScopedDeviceListReleaser`, `clearNodes()`, or
   `restartPlayback()`.

   The engine-level `midiInputs` and `midiOutputs` containers can still be swapped and the usual
   change message can still be sent. The active audio graph continues using the same node tree and
   playhead.

4. For affected contexts, use the existing reload path.

   The existing graph rebuild behavior remains the fallback for real route invalidation. That keeps
   the fix surgical and avoids changing recording, insert, click, clock, or MTC behavior in the
   first pass.

## Implementation Scope

Prefer new private helpers inside Tracktion's playback module:

- `DeviceManager`: compute changed MIDI device identities and decide which active contexts need
  reload after `applyNewMidiDeviceList()`.
- `EditPlaybackContext`: answer whether a set of affected MIDI input/output identities is used by
  the current context.

Avoid public API changes unless the private helper becomes unreasonably coupled.

Do not add transport resume logic as the primary fix. It may hide a stop, but it cannot remove the
audio dropout caused by graph teardown.

## Test Plan

Add focused automated coverage for the classification behavior.

- Unused MIDI device discovery does not call the destructive context reload path while an audio-only
  edit is playing.
- An affected MIDI output route still triggers reload.
- An affected monitored or armed MIDI input still triggers reload.

Where direct testing of `DeviceManager::reloadAllContextDevices()` is too private, use a small
Tracktion-internal test hook or a minimal private helper test rather than broad Rock Hero adapter
behavior.

Manual verification:

- Start Rock Hero, restore a project, press space before the startup MIDI scan finishes.
- Unplug and replug an unused MIDI device during audio-only playback.
- Confirm no cursor jump, no pause, and no audible dropout.
- Then route click or MIDI clock to a MIDI device and confirm hotplugging that device still causes
  an intentional rebuild/interruption.

## Open Questions

- Should unused MIDI output instances inside an active context be refreshed immediately, or can they
  wait until the next graph-affecting edit/device change?
- Should audio device-list changes receive the same invalidation gate in the same patch, or should
  the first patch focus only on the confirmed MIDI startup/hotplug regression?
- Should Tracktion expose a debug counter or scoped callback for context reloads to make this
  regression easier to test without weakening production encapsulation?
