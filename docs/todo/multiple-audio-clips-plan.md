# Multiple Audio Clips Per Track

Status: planned. Do not implement until the single-clip `AudioClipId` change has landed and the
track/session/audio edit boundary is stable.

## Goal

Let each `core::Track` contain multiple placed `core::AudioClip` values while keeping `Session` as
the only owner of clip identity, ordering, and mutation. The model should become capable of
representing normal editor operations such as adding, removing, moving, trimming, and replacing
clips without letting callers put the session and audio backend out of sync.

The main design constraint is that `rock-hero-core` remains framework-free. Tracktion and JUCE
details stay in `rock-hero-audio`; core owns only the project model and pure rules.

## Current State

The project currently has the right transitional shape:

- `Track` stores `std::optional<AudioClip> audio_clip`.
- `AudioClip` has a stable `AudioClipId`, but callers construct clips with an invalid id.
- `Session::commitTrackAudioClip` assigns the id and stores the backend-accepted clip.
- There is an explicit TODO near the session storage saying single-clip replacement should become
  add/remove/move commands later.

That is intentionally honest while the editor supports only one clip per track. Moving directly to
`std::vector<AudioClip>` without the command API would make the data model imply multi-clip support
before the invariants exist.

## Target Model

The eventual `Track` shape should be:

```cpp
struct Track
{
    TrackId id;
    std::string name;
    std::vector<AudioClip> audio_clips;
};
```

`audio_clips` should be owned and ordered by `Session`. Callers may read tracks through const
views, but all edits must go through `Session` methods.

The ordering rule should be:

- Clips are sorted by `AudioClip::position`, then by `AudioClipId` as a stable tie-breaker.
- A later overlap policy can reject or allow overlaps per track type, but the first multi-clip
  implementation should make the policy explicit rather than accidental.

## API Direction

Prefer command-shaped `Session` methods that make identity and failure visible:

```cpp
std::optional<AudioClipId> commitTrackAudioClip(TrackId track_id, AudioClip audio_clip);
bool removeTrackAudioClip(TrackId track_id, AudioClipId clip_id);
bool moveTrackAudioClip(TrackId track_id, AudioClipId clip_id, TimePosition new_position);
bool trimTrackAudioClip(TrackId track_id, AudioClipId clip_id, TimeRange new_source_range);
```

The add operation should return the session-assigned `AudioClipId`. Remove/move/trim can return
`bool` until the editor needs richer error reporting. If richer failure reporting becomes useful,
prefer `std::expected` with a small project-owned error enum rather than exceptions or strings.

Do not expose mutable track or clip handles. The compiler should make bypassing the session
mutation boundary inconvenient or impossible.

## Backend Sync Boundary

The long-term design should not let a controller independently mutate both `Session` and `IEdit`
with no compiler-enforced ordering. Multi-clip editing makes that risk larger because each clip has
stable identity and multiple possible mutation commands.

Before or during this work, introduce a clearer coordination boundary for editor clip commands. A
reasonable direction is a project-owned use-case/service object that owns the command sequence:

1. Validate the requested clip command against `Session`.
2. Ask `IEdit` or a future audio edit port to apply the corresponding backend change.
3. Commit the accepted change into `Session`.
4. Return the assigned `AudioClipId` or a typed failure.

`EditorController` should eventually depend on that command boundary instead of directly holding
both a mutable `Session` and an audio edit port for clip mutations.

## Audio Edit Port Changes

The current audio edit port is single-clip shaped. Multi-clip support should either add explicit
clip commands or replace the track contents as a transaction:

```cpp
bool addTrackAudioClip(core::TrackId track_id, const core::AudioClip& audio_clip);
bool removeTrackAudioClip(core::TrackId track_id, core::AudioClipId clip_id);
bool moveTrackAudioClip(
    core::TrackId track_id, core::AudioClipId clip_id, core::TimePosition new_position);
```

The explicit command shape maps well to editor behavior and focused tests. If Tracktion makes
incremental mutation awkward, a transactional `setTrackAudioClips(track_id, span)` may be simpler
inside `rock-hero-audio`, but the public coordination layer should still expose command intent.

## Implementation Phases

1. Add pure core tests first.
   - Default `Track` has an empty `audio_clips` vector.
   - Adding clips assigns monotonically increasing ids.
   - Caller-supplied clip ids are ignored.
   - Clips remain sorted after add and move.
   - Removing a missing clip fails without changing state.
   - Timeline calculation uses every clip on every track.

2. Convert `Track` storage from optional to vector.
   - Update `TrackViewState` projection to keep showing a single asset only if the UI still has one
     visible waveform row.
   - Replace `anyTrackHasClip` with a vector-aware check.
   - Keep one loaded clip in the UI until the editor grows multi-clip controls.

3. Update `Session` commands.
   - Change `commitTrackAudioClip` to append and return `std::optional<AudioClipId>`.
   - Add internal helpers for finding clips by id.
   - Recalculate the project timeline from all clips.
   - Keep `tracks()` read-only.

4. Update audio adapter behavior.
   - Start with one Tracktion audio track containing multiple wave clips.
   - Map `AudioClipId` to Tracktion clip identity internally if needed.
   - Keep all Tracktion mutation on the message thread.
   - Preserve current validation rules for asset duration, source range, and timeline placement.

5. Update controller and UI tests.
   - Verify a load command receives the assigned clip id.
   - Verify failed backend edits do not mutate session state.
   - Verify play/stop enabledness works when any track has at least one clip.
   - Verify visible timeline duration comes from all committed clips.

## Open Design Decisions

- Overlap policy: reject overlaps on the same track initially, or allow them because Tracktion can
  layer clips? For a rhythm-game backing track, rejecting overlap is probably simpler until there is
  a real compositing use case.
- Clip identity persistence: ids may be session-local for now, but song serialization will need a
  stable persisted identity if clips can be referenced by automation, edits, or saved selections.
- UI representation: one waveform per track may be enough for now, but real multi-clip editing will
  need clip rectangles, selection state, drag handles, and trim handles.
- Error type: keep `bool`/`std::optional` while failures are simple; move to `std::expected` once
  user-visible error reasons matter.

## Recommended First Implementation

Do not make `Track` a vector-only model as a cosmetic refactor. The first real implementation
should pair the storage change with `Session` commands and tests in the same commit. That keeps the
model, mutation API, and invariants aligned.

The safest first feature slice is:

- `Track::audio_clips` as `std::vector<AudioClip>`.
- `Session::commitTrackAudioClip` appends one backend-accepted clip and returns its id.
- `Session::removeTrackAudioClip` removes by id.
- Timeline calculation covers all clips.
- UI still behaves as a single loaded backing-track workflow by using the first or only clip until
  dedicated multi-clip editor controls exist.
