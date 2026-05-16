# Tone Rack Plan

## Goal

Build the instrument tone system around one user-facing instrument lane with multiple
tone slots. Each tone slot represents a plugin chain that can be selected, blended, or automated
without exposing Tracktion tracks or racks directly to the editor/game domain.

The first durable backend should compile those tone slots into a single Tracktion `RackType` on the
instrument track. The rack contains parallel plugin branches, one branch per tone slot, with a
controllable gain point at each branch output.

## Design Direction

- Keep the public app model project-owned: `ToneSlot`, `ToneChain`, and eventually `ToneGraph`.
- Keep Tracktion `RackType`, rack connections, and plugin instances inside `rock-hero-common/audio`.
- Use one visible instrument lane in the editor and game.
- Do not model tones as visible Tracktion tracks.
- Do not rely on plugin bypass for seamless switching.
- Keep seamless-switch tones loaded, prepared, enabled, and actively processing.
- Switch audible tones by changing branch output gains with short ramps.
- Treat hidden parallel tracks as a fallback backend only if `RackType` becomes a blocker.

## Why RackType First

`RackType` matches the long-term shape better than one hidden Tracktion track per tone:

- A tone system is one instrument route, not a collection of song tracks.
- The rack can hold more than two parallel chains.
- Branching, blending, and future containers fit naturally in a graph-like backend.
- The complexity stays inside the audio adapter instead of leaking track lifecycle details into
  editor or game code.
- The app-facing model can stay stable if the Tracktion implementation changes later.

Hidden tracks are simpler for a quick prototype, but they add routing and lifecycle problems around
input assignment, monitoring, latency alignment, track hiding, track cleanup, and ensuring muted
tracks continue processing.

## Initial Scope

The first implementation should stay intentionally small:

1. Add a project-owned tone model in `rock-hero-common/audio`.
2. Support a fixed set of tone slots for the active instrument route.
3. Allow a tone slot to contain a linear list of VST3 plugin references.
4. Compile tone slots into one Tracktion rack on the instrument track.
5. Insert each plugin chain as a parallel rack branch.
6. Add one controllable branch gain point per tone slot.
7. Provide an operation to select the active tone slot.
8. Crossfade from the previous active branch to the new active branch over a short ramp.
9. Keep all slots that need seamless switching actively processing at zero output gain when
   inactive.

## Non-Goals For The First Pass

- No nested Tracktion racks. Tracktion `RackInstance` cannot be added to another rack.
- No arbitrary graph editor UI.
- No plugin parameter automation UI inside the rack.
- No parallel plugin containers inside a single tone slot yet.
- No CPU-saving unload policy for inactive seamless-switch slots.
- No persistence format commitment beyond what is needed for the current editor session.

## Audio Adapter Responsibilities

`rock-hero-common/audio` should own the Tracktion-specific work:

- Create or locate the instrument rack.
- Add/remove plugin instances inside the rack.
- Create rack connections for each tone branch.
- Maintain branch gain control points.
- Apply gain ramps on the message thread or through Tracktion automation-safe mechanisms.
- Keep graph mutations away from the audio callback.
- Translate plugin load and rack build failures into project-owned audio errors.

The domain-facing API should describe requested tone operations, not Tracktion objects.

## Editor UI Direction

The editor should keep the bottom instrument panel as the control surface for this feature.
The first UI can be minimal:

- List tone slots.
- Show the plugin chain for the selected tone slot.
- Add a VST3 plugin to the selected tone slot.
- Select the currently audible tone slot.
- Show load or routing failures in project-owned terms.

The arrangement track viewport should remain focused on the song timeline, not on exposing one
Tracktion track per tone.

## Testing Strategy

Prefer fast tests over project-owned state transitions before testing Tracktion integration:

- Unit-test tone slot selection and gain-vector decisions without Tracktion.
- Unit-test crossfade planning from one active slot to another.
- Adapter-test rack creation and branch construction with focused Tracktion-backed tests.
- Adapter-test that inactive seamless slots remain enabled and loaded rather than bypassed or
  processing-disabled.
- Add regression coverage for plugin load failure and partial rack build cleanup.

Full audio-device or real plugin tests should remain optional integration tests, not the main
coverage strategy.

## Open Questions

- What project-owned gain node should terminate each branch: Tracktion volume/pan, rack I/O gain,
  or a small custom internal gain plugin?
- Should the first implementation support only one selected tone at a time, or a small N-way blend
  value from the start?
- Where should tone slot state persist once the runtime model stabilizes?
- How much warm-up processing should run after loading a heavy plugin chain before the UI marks the
  tone slot as seamless-ready?
