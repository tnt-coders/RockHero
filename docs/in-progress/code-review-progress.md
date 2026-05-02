# Code Review Progress

Status: in progress as of 2026-05-01.

This is a working handoff note for the repository-wide design and testability review. It records
where the discussion currently stands, what design choices have already been made, and where to
resume. It is not intended to replace the durable design docs or TODO plans.

## Review Goal

Review the repository module by module for long-term structure, automated testability, and clear
separation between pure domain logic, adapters, UI presentation, and app composition.

The review style is intentionally expert-level C++ and CMake oriented. Each class walkthrough should
explain the purpose of the type, any non-obvious implementation details, and the architectural
reasoning behind the design. After each class, pause and ask quiz questions that test genuine
understanding rather than rote recall.

## Completed So Far

### `rock-hero-core`

The core library review was completed before moving to audio.

Key conclusions:

- `rock-hero-core` is correctly positioned as the framework-free domain/model library.
- Time value wrappers such as `TimePosition` and `TimeDuration` are valuable because they prevent
  accidentally passing a duration where an absolute position is expected, and vice versa.
- Timeline equality is intentionally exact. Tolerance-based comparisons should be explicit at the
  algorithm call site instead of hidden inside `operator==`.
- `TimePosition` and `TimeDuration` use manual equality with `std::is_eq(lhs.seconds <=> rhs.seconds)`
  to preserve exact floating-point equality semantics while avoiding `-Wfloat-equal` under the
  shared warnings-as-errors policy.
- `TimeRange` is the right shared value concept for simple start/end ranges. A broad `ITimeline`
  interface is not justified yet just to expose start and end values.
- `Session` is now the correct owner for editable track state and project timeline state.
- `Track` reads are exposed as const views. All mutation goes through `Session` methods, which
  centralizes model updates but does not yet fully enforce backend/session synchronization.
- `Session::setAudioClip` is the current temporary single-clip setter. Editor orchestration should
  call it only after the backend has accepted the corresponding audio edit.
- `setAudioClip` has an explicit TODO to replace it with `addAudioClip` and `removeAudioClip` once
  the project supports more than one clip per track.
- `AudioClip` now models the long-term direction better than the older asset-only shape:
  - `id`
  - `asset`
  - `asset_duration`
  - `source_range`
  - `position`
- `AudioClipId` exists now and is assigned by `Session`, but each `Track` still stores only one
  optional clip for the current single-file workflow.

Relevant follow-up docs:

- `docs/todo/multiple-audio-clips-plan.md`
- `docs/todo/thread-safe-transport-readback.md`

### Test Framework Direction

The testing framework decision was revisited independently of refactor cost.

Current direction:

- Keep Catch2 as the test runner and assertion framework.
- Continue preferring stateful hand-written fakes for project-owned interfaces.
- Introduce Trompeloeil only for tests where interaction semantics are the behavior under test,
  such as strict call counts, call ordering, forbidden calls, or failure escalation.
- Do not mock JUCE or Tracktion directly.
- Do not implement centralized fake extraction until the folder layout, namespace, include style,
  and CMake target strategy are explicitly chosen.

Relevant follow-up doc:

- `docs/todo/centralized-test-fakes-and-trompeloeil-plan.md`

## `rock-hero-audio` Review Progress

The audio module review has started. The top-level assessment is that the library is broadly in the
right role: it is the Tracktion/JUCE adapter layer and should not absorb gameplay rules, scoring
policy, generic editor behavior, or general domain modeling.

### Completed Audio Class Walkthroughs

#### `TransportState`

Reviewed and renamed back from `TransportStatus` to `TransportState`.

Current intent:

- `TransportState` is the coarse transport snapshot.
- It currently describes whether playback is advancing.
- It deliberately does not include position.
- It deliberately does not include duration.

Important reasoning:

- Position changes continuously and should not be pushed through listener callbacks.
- Duration is loaded-content or project timeline state, not transport state.
- UI code that needs smooth cursor motion should poll position at its own cadence.
- Future gameplay/scoring timing should use a separate playback clock, not this state snapshot.

#### `ITransport`

Reviewed in detail and updated during the review.

Current intent:

- `ITransport` is the project-owned transport command and coarse-state boundary.
- It owns play, pause, stop, seek, live message-thread state reads, live message-thread position
  reads, and coarse state listeners.
- The public contract is message-thread-only. `state()`, `position()`, commands, listener
  registration, and listener callbacks must all be used from the JUCE message thread today.

Important API decisions:

- `state()` is a live read from the adapter, not a cached listener-published snapshot.
- `position()` is also a live read from Tracktion through the adapter.
- Neither `state()` nor `position()` is currently thread-safe.
- Listeners receive only coarse `TransportState` changes and do not receive position updates.
- `seek()` stays on `ITransport` because seeking is a normal DAW transport command that moves the
  playback cursor used by the engine.
- Do not extract `seek()` and `position()` into an `ICursor` interface. `seek()` is a side-effecting
  command; realtime timing readback is telemetry. Mixing those contracts would be easy to misuse.
- A future `IPlaybackClock` is the preferred thread-safe/lock-free readback surface for game,
  scoring, render, latency calibration, and debug timing consumers.
- The nested listener type remains named `Listener` for consistency. A more specific nested name
  such as `StatusListener` should only be considered if the owning type grows multiple independent
  listener surfaces or the generic name starts obscuring semantics.

Important implementation details already discussed:

- In a JUCE app, the main UI thread is the message thread.
- The current editor cursor path is acceptable because the editor view reads position from the
  message thread.
- An Engine-owned atomic mirror could provide thread-safe and lock-free reads later, but a
  message-thread mirror would not be sample-accurate unless it is published from an audio-derived
  timing path.
- Tracktion has lower-level atomic timing internals, but reaching them safely through public API is
  not currently a clean project-wide contract. Any such integration must stay isolated in
  `rock-hero-audio`.

Relevant follow-up doc:

- `docs/todo/thread-safe-transport-readback.md`

### Audio Review Interrupted By Design Fixes

Before continuing through the remaining audio classes, the review found and corrected several
boundary problems:

- Session track mutation was too easy to bypass.
- Updating session state and updating the backend were not clearly enforced as one coordinated
  workflow.
- The controller could risk updating core state independently from backend acceptance.
- Track audio source modeling was too asset-centric for the eventual clip/timeline design.

Short-term mitigation now in code:

- `Session::tracks()` returns a const vector reference.
- `Session::findTrack()` returns a const pointer.
- Mutable track lookup is internal to `Session`.
- `Session::setAudioClip()` assigns clip ids and recomputes the session timeline.
- `IEdit::loadAudioAsset()` asks the playback backend to inspect and load an asset, then return the
  accepted `AudioClip`.
- The durable session model is updated only after backend acceptance.

Long-term requirement:

- A clearer setter alone is not enough. The eventual design should provide a
  compiler-enforced boundary that prevents code from accidentally placing the session model and
  playback backend out of sync.
- A future editor command/use-case layer should likely coordinate validation, backend mutation, and
  session update as one operation.

Relevant follow-up doc:

- `docs/todo/multiple-audio-clips-plan.md`

## Current Position In The Walkthrough

Resume the code review at `IEdit`.

`IEdit` has been discussed because of the session/backend synchronization flaw, but it has not yet
received the same full class-by-class walkthrough and quiz treatment that `TransportState` and
`ITransport` received.

When resuming:

1. Re-walk `IEdit` from the current code, not from the earlier `setTrackAudioSource` design.
2. Explain why `loadAudioAsset()` lives behind the audio edit port.
3. Explain why it returns an accepted `core::AudioClip` with an invalid id.
4. Explain why it currently returns `std::optional` and when `std::expected` would be justified.
5. Explain the current limitation that only the most recently loaded track clip must behave
   correctly until stem/multi-track playback semantics are implemented.
6. Quiz after the class before moving on.

Suggested expert-level quiz questions for `IEdit`:

- Why is `loadAudioAsset()` on the audio edit port instead of on `Session` or `Track`?
- Why does `loadAudioAsset()` return a framework-free clip instead of writing directly to
  `Session`?
- What invariant is protected by updating the clip in `Session` only after the backend accepts it?
- Why is `std::optional<core::AudioClip>` acceptable for the current backend mutation result, and
  what concrete requirement would push this toward `std::expected<..., Error>`?
- Why is the current single-track-applied note important for callers and tests?

## Remaining Audio Classes To Review

After `IEdit`, continue with:

- `IThumbnail`
- `IThumbnailFactory`
- `ScopedListener`
- `Engine`
- `TracktionThumbnail`
- Audio CMake/test target structure
- Audio test coverage

Important coverage questions still to answer:

- Do audio port tests cover project-owned contract behavior without unnecessarily constructing
  Tracktion/JUCE adapters?
- Do engine integration tests cover the current single-file playback contract deeply enough?
- Are failure paths for missing files, invalid audio, invalid source ranges, and rejected backend
  clips covered?
- Are listener notification semantics covered without over-specifying implementation details?
- Are thumbnail/proxy behaviors covered at the right level?
- Which repeated fakes should stay local for now versus move into shared test support later?

## Known Open Design Threads

### Error Reporting In `IEdit`

The current `bool`/`std::optional` style is acceptable while callers only need success/failure.

Move toward `std::expected` when the UI or workflow needs structured user-visible failure reasons,
such as missing file, unsupported file, invalid source range, Tracktion rejection, or path
conversion failure.

If `Engine` catches ordinary `std::exception` failures from adapter code later, preserving
`exception.what()` can be useful, but it should be converted into a project-owned error type rather
than leaking framework exception behavior through broad interfaces.

### Future Thread-Safe Playback Readback

Do not build this immediately for the current editor-only use case.

Add `IPlaybackClock` when gameplay, scoring, latency calibration, render code, or debug timing
requires lock-free playback-derived reads outside the message thread.

### Timeline Modeling

Do not introduce a broad `ITimeline` interface just for range storage.

Use `TimeRange` as the value object. Add more specific concepts later only when behavior justifies
them, such as project timeline state, visible editor range, tempo map conversion, or playback clock
readback.

### Multi-Clip Tracks

Do not switch `Track` to `std::vector<AudioClip>` merely as a cosmetic cleanup.

Make that change when it can land with the matching `Session` commands, invariants, tests, and
audio adapter behavior.

## Recent Review-Adjacent Fixes

Several fixes happened during the review because new warnings-as-errors and tests exposed real or
near-real issues:

- Transport comments were corrected after `TransportState` stopped carrying position.
- `state()` and `position()` comments now call out live message-thread reads.
- The stop/pause UI regression was fixed after the transport state/position split exposed stale
  stop-enabledness behavior.
- Tests were adjusted to satisfy `bugprone-unchecked-optional-access` without weakening the
  assertions.
- The shared build policy and lint configuration work is complete enough to continue code review
  without revisiting it unless CI exposes a new problem.

## Resume Checklist

Before continuing:

- Re-read `IEdit` and its tests.
- Keep the conversation centered on architecture, purpose, implementation details, and testability.
- Pause after each class for quiz questions.
- Do not implement test-coverage expansions until the current class walkthrough identifies concrete
  gaps.
- Keep short-term fixes narrow if the review exposes an actual correctness or boundary problem.
