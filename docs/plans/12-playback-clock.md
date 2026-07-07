# Plan 12 — Playback Clock (thread-safe audio-derived time readback)

Status: Ready — 2026-07-06 — baseline `refactor @ 3c7febe0`.

## Goal

Any thread in either product can read "where is playback right now" as a cheap, wait-free
snapshot, and render-loop consumers can turn those snapshots into perfectly smooth, monotonic
per-frame time. Concretely: a project-owned `IPlaybackClock` port backed by an Engine-owned atomic
mirror of Tracktion's latency-compensated audible playback time, plus a pure
`PlaybackClockExtrapolator` that slaves a monotonic clock to the published audio ticks with
clamped drift correction and defined seek/pause/loop snap rules. This is the single playback time
source for the game highway (docs/plans/25-note-highway-3d.md), scoring timestamps
(docs/plans/24-scoring-star-power-failure.md), the editor 3D preview
(docs/plans/44-editor-3d-preview.md), and calibration sampling
(docs/plans/13-audio-device-settings-and-calibration.md).

## Non-goals

- No change to `ITransport` command semantics: `play()`, `pause()`, `stop()`, `seek()` stay
  message-thread commands (decision restated below). No `ICursor` interface.
- No duration, loaded-range, or tempo-map data in the clock. Duration is content state, not
  realtime telemetry; tempo conversion stays pure `common/core` timeline math.
- No detection-event timestamping. Plan 22 timestamps analysis results in audio-sample time at the
  audio callback; this clock is the chart-time correlation surface, not the sample tap.
- No GameplaySession, no game render loop, no UI. Those are plans 21/20/25; this plan only gives
  them the timing surface they consume.
- No editor cursor-overlay migration in the required phases (recorded as an open question).
- No Tracktion submodule patch unless the audio-derived-publish investigation proves every
  public-API option unworkable (rollback notes cover this).

## Constraints

Applicable subset of the roadmap constraint block (see docs/plans/00-roadmap.md):

- (a) **Layering**: common never depends on editor or game code; editor and game never depend on
  each other. Both products need this clock, so everything in this plan lands in
  `rock-hero-common` first, with tests, before any game code consumes it. Tracktion headers stay
  isolated to `rock-hero-common/audio` implementation files.
- (b) **Public-header minimalism**: only the port, the snapshot value, and the extrapolator become
  public headers. The atomic storage class stays private to the library (`src/`), per
  docs/design/architectural-principles.md "Ports and Adapters" and "Placement Procedure for New
  Files".
- (c) **Naming firewall**: the commercial real-guitar game that inspired this project is never
  named; use "RS" or neutral phrasing.
- (h) **Builds**: all build/test/lint commands go through `.agents/rockhero-build.ps1` (usage in
  `.agents/README.md`), never raw cmake/ctest/ninja. Intermediate phases run only the checks their
  changes warrant; the final acceptance phase is the sanctioned bundle as separate invocations.

Design-doc rules this plan implements directly:

- docs/design/architecture.md "Timing and Latency": the audio thread is the single source of truth
  for timing; game view and scoring derive all state from transport position and never maintain
  independent clocks; scoring comparisons happen in audio-sample-derived time and mixing
  render-thread time into scoring is prohibited.
- docs/design/architecture.md "Threading Model" / "Thread Communication": transport position
  crosses threads via `std::atomic`; all audio-thread communication is lock-free.
- docs/design/architectural-principles.md "Time Must Be a Dependency": consumers receive injected
  time (snapshots, monotonic now); nothing in core logic reads wall clocks directly.
- docs/design/architectural-principles.md "Keep Threading at the Boundary": atomics and publish
  cadence live in the audio adapter; consumers get plain value snapshots.

No design-doc changes are required: architecture.md already mandates exactly this shape.

## Current state inventory

- `ITransport` is documented message-thread-only, with `state()`/`position()` as current reads and
  listeners carrying only coarse state:
  `rock-hero-common/audio/include/rock_hero/common/audio/transport/i_transport.h` (contract note
  at lines 14-24, `position()` doc at lines 91-100).
- `TransportState` carries only `playing` — duration was already removed:
  `rock-hero-common/audio/include/rock_hero/common/audio/transport/transport_state.h:11-24`.
- `Engine` is the ports-and-adapters facade multiply inheriting eight ports (`ITransport`,
  `ISongAudio`, `IAudioDeviceConfiguration`, `IAudioMeterSource`, `IPluginHost`, `ILiveInput`,
  `ILiveRig`, `IThumbnailFactory`):
  `rock-hero-common/audio/include/rock_hero/common/audio/engine/engine.h:57-64`. Per-port
  translation units live in `rock-hero-common/audio/src/engine/` (`engine_transport.cpp`,
  `engine_song_audio.cpp`, `engine_device_config.cpp`, ...), with `engine.cpp` as the assembly
  file and `engine_impl.h` as the declaration-only private surface.
- `Engine::position()` already reads Tracktion's latency-compensated audible time while playing:
  it traverses `getCurrentPlaybackContext()->getAudibleTimelineTime()` on the message thread
  (`rock-hero-common/audio/src/engine/engine_transport.cpp:148-164`). This is a **correction to
  the absorbed doc** (the thread-safe-transport-readback todo plan, since deleted), which claimed
`position()`
  read only `TransportControl::getPosition()`. The read is still message-thread-only and still
  traverses Tracktion-owned pointer state, so the absorbed doc's core conclusion stands: this path
  must never be called from game/render/scoring threads.
- Auto end-of-file stop runs on the message thread via `valueTreePropertyChanged` watching
  `tracktion::IDs::position` (`engine_transport.cpp:31-44`); `seek()` clamps through
  `clampToLoadedRange` (`engine_transport.cpp:135-140`).
- Vendored Tracktion facts (re-verified for this plan):
  - `EditPlaybackContext::audiblePlaybackTime` is `std::atomic<double>`
    (`external/tracktion_engine/modules/tracktion_engine/playback/tracktion_EditPlaybackContext.h:206`).
  - `EditPlaybackContext::getAudibleTimelineTime()` loads that atomic
    (`.../tracktion_EditPlaybackContext.cpp:1091-1093`).
  - `PlayHeadPositionNode` writes it from the audio graph with latency compensation
    (`.../playback/graph/tracktion_PlayHeadPositionNode.h:17`).
  - Tracktion's own live-position read reaches the atomic **through**
    `transport.playbackContext` pointer state (`.../tracktion_TransportControl.cpp:603-610`),
    which the message thread rebuilds/frees — so the safe public access path is not a plain
    atomic read. Consumers outside the adapter must never traverse it.
- `TimePosition`/`TimeDuration`/`TimeRange` value types already exist in
  `rock-hero-common/core/include/rock_hero/common/core/timeline/timeline.h` (TimeRange with
  `duration()`/`contains()`/`clamp()` at lines 88-125). Steps 1-3 of the absorbed doc's migration
  plan are therefore already complete on this baseline.
- Test infrastructure: `rock_hero_common_audio_tests` builds contract tests over fakes
  (`rock-hero-common/audio/tests/test_transport.cpp`) and headless Engine adapter tests
  (`test_engine.cpp` — construction, seek, arrangement load, all without an audio device). The
  test target already adds `../src` to its private include path for adapter internals
  (`rock-hero-common/audio/tests/CMakeLists.txt:43-44`).
- Editor render-cadence readers use `juce::VBlankAttachment` on the message thread
  (`rock-hero-editor/ui/src/timeline/cursor_overlay.h:119`, `track_viewport.h:316`,
  `tone_track_view.h:229`) — they remain correct on the existing `ITransport::position()` path.
- `rock-hero-game/` is a build-system skeleton (placeholder libs, 81-line window shell); the first
  out-of-message-thread consumer (the game render loop) arrives with
  docs/plans/20-game-architecture-and-render-stack.md and docs/plans/25-note-highway-3d.md, which
  is exactly the absorbed doc's trigger condition for building this now.

Verified against code on 2026-07-06, refactor @ 3c7febe0.

## Dependencies

Upstream (none block the start of this plan — it is a foundation plan):

- docs/plans/00-roadmap.md — execution ordering; this plan is scheduled early because milestone 0
  needs it for highway scrolling.
- docs/plans/20-game-architecture-and-render-stack.md Phase 0b decides which thread the game
  render loop runs on. The clock API is deliberately thread-agnostic (any-thread `snapshot()`), so
  this plan does not wait for that gate; only the *consumer wiring* does.

Downstream consumers (recorded in their Dependencies sections as well):

- docs/plans/21-game-audio-engine-and-session.md — GameplaySession exposes `IPlaybackClock` to
  gameplay; publishes a non-1.0 `playback_rate` once speed control exists.
- docs/plans/24-scoring-star-power-failure.md — scoring timestamps and the coherent-snapshot
  revisit (open question 1 below).
- docs/plans/25-note-highway-3d.md — highway render consumes the extrapolator each frame.
- docs/plans/44-editor-3d-preview.md — editor preview consumes the same extrapolator (this is why
  it lives in common, per constraint (a)).
- docs/plans/13-audio-device-settings-and-calibration.md — calibration capture samples the clock.
- docs/plans/28-practice-mode.md — requires that plan 12/21 interfaces carry a playback speed
  factor and survive loop-region seeks from day one; this plan bakes both into the snapshot and
  the extrapolator snap rules (only 1.0 is ever published until 21/28 land).
- docs/todo/smooth-scroll-follow-evaluation.md — the smooth-scroll follow evaluation is an owned,
  pending user decision; if approved, its implementation would consume the extrapolator. Reference
  only — this plan does not decide or duplicate it.

## Decisions already made

Restated from the absorbed thread-safe-transport-readback todo doc (since deleted; content
re-verified against `refactor @ 3c7febe0` and corrected where noted in the inventory):

1. `seek()` stays on `ITransport`: it is a side-effecting message-thread command, not cursor
   telemetry. No `ICursor` interface mixing message-thread-only and thread-safe methods.
2. The read-only timing surface is named `IPlaybackClock` (not "cursor" — the surface serves
   render, scoring, calibration, and debug timing, not just a visible playhead).
3. Duration stays out of the clock and out of `TransportState` (already true on this baseline).
   Duration/loaded-range/tempo data flow through timeline and session state.
4. `IPlaybackClock::snapshot()` must not traverse Tracktion pointers, allocate, block, or call
   JUCE. It reads only RockHero-owned atomic storage.
5. Hot position storage is integer nanoseconds in `std::atomic<std::int64_t>` with
   `memory_order_relaxed` — telemetry semantics; the clock read publishes no ownership of other
   data. Nanoseconds are finer than sample resolution and avoid relying on lock-free FP atomics.
6. Per-field incoherence (position and playing not from the same audio block) is accepted for v1.
   If a consumer later needs a coherent multi-field record, the internal storage becomes a
   project-owned seqlock while the public API stays unchanged.
7. Message-thread transport operations publish immediate boundary values (construction, load,
   seek, play, pause, stop, auto end-of-file stop) so the clock is useful before the first audio
   block and after playback ends.
8. Publishing prefers the audio-derived timing path; consumer threads never publish. Direct
   `getCurrentPlaybackContext()` traversal is forbidden outside the Tracktion adapter.
9. `Engine` implements `IPlaybackClock` alongside its other ports.

From docs/design/architecture.md ("Timing and Latency", "Thread Communication"):

10. The audio thread is the single source of truth for timing; consumers never maintain
    independent clocks; transport position crosses threads via `std::atomic`.
11. Scoring comparisons happen in audio-derived time with calibration offsets applied
    consistently; mixing render-thread time into scoring is prohibited (the extrapolator output
    is for *rendering*; plan 24 scores against detection timestamps plus this clock's chart-time
    correlation, never against extrapolated frame time).

New decisions this plan makes (rationale inline, flagged for reviewer attention):

12. The snapshot gains two fields beyond the absorbed doc's `{position, playing}`:
    `monotonic_capture_time` (steady-clock stamp of the publish) and `playback_rate` (1.0 until
    docs/plans/21-game-audio-engine-and-session.md publishes otherwise). Capture stamps turn
    extrapolation from a jitter-filtering problem into arithmetic
    (`position + (now - captured_at) x rate`) and make the clock robust to publisher stalls; the
    rate field is docs/plans/28-practice-mode.md's non-negotiable day-one plumbing.
13. Phase 3 v1 publishes from an adapter-owned message-thread republisher that reads
    `getAudibleTimelineTime()` (the same read `Engine::position()` already performs safely today)
    and stamps it — rather than starting with a custom audio-graph node. The published *value* is
    still audio-derived and latency-compensated (it originates in `PlayHeadPositionNode`); only
    the republish cadence is message-thread. Capture stamps make cadence jitter harmless to the
    extrapolator, and the audio thread stays untouched. The audio-graph tap remains the recorded
    escalation path behind the same API if measured publish stalls ever exceed what extrapolation
    hides (measured via plan 25's debug overlay / plan 20's diagnostics layer).

## Open questions for the user

1. **Snapshot time domain for scoring correlation.** Options: (A) timeline nanoseconds only (v1
   as specified); consumers derive sample positions via the device sample rate when needed;
   revisit a coherent `{timeline time, output sample position}` pair (seqlock upgrade, decision 6)
   when docs/plans/24-scoring-star-power-failure.md defines its provisional-hit contract. (B) Add
   an output-stream sample-position field now. **Recommendation: A** — plan 22 timestamps
   detections in sample time at the audio callback independently of this clock, so nothing needs
   the pair yet, and the upgrade path is API-stable.
2. **Editor cursor migration / `ITransport::position()` retirement.** Options: (A) migrate the
   editor cursor overlay to `IPlaybackClock` + extrapolator as an optional phase here and delete
   `ITransport::position()`. (B) Keep the editor on the message-thread `position()` path (it is
   correct there) and revisit when docs/plans/44-editor-3d-preview.md and the smooth-scroll
   decision (docs/todo/smooth-scroll-follow-evaluation.md, awaiting your call) create real
   pressure. **Recommendation: B** — no editor behavior improves today, and retiring
   `position()` prematurely couples this foundation plan to editor UI churn.
3. **Extrapolation feel defaults.** Proposed: snap threshold 120 ms, drift slew limit 5% of
   elapsed frame time, pause = exact hold, resume/seek/loop-wrap = snap. Options: (A) accept as
   starting defaults, tune live during plan 25 with the debug overlay; (B) review the numbers
   now. **Recommendation: A** — they are policy-struct parameters, deliberately cheap to tune.

## Phased implementation

### Phase 1 — Clock port, snapshot value, and atomic storage

**Scope.** Create the `clock/` feature folder in `rock-hero-common/audio` (a feature earns its
folder at its first file, per docs/design/architectural-principles.md "Feature Folders"; the clock
is a distinct contract from the `transport/` command surface by decision 1/2).

New public headers (Doxygen per docs/design/documentation-conventions.md):

- `rock-hero-common/audio/include/rock_hero/common/audio/clock/playback_clock_snapshot.h` —
  `struct PlaybackClockSnapshot { core::TimePosition position{}; std::chrono::nanoseconds
  monotonic_capture_time{0}; double playback_rate{1.0}; bool playing{false}; }` with defaulted
  equality. `monotonic_capture_time == 0ns` means "no publish yet".
- `rock-hero-common/audio/include/rock_hero/common/audio/clock/i_playback_clock.h` —
  `IPlaybackClock` with `[[nodiscard]] virtual PlaybackClockSnapshot snapshot() const noexcept = 0;`
  documented as callable from any thread, wait-free, telemetry-only (decisions 4-6), following
  `ITransport`'s protected-constructor interface boilerplate style.

New private storage (`src/` first, per the placement procedure — no consumer outside the library):

- `rock-hero-common/audio/src/clock/atomic_playback_clock.h/.cpp` — `AtomicPlaybackClock final :
  public IPlaybackClock`, four relaxed atomics (`std::int64_t` position ns, `std::int64_t` capture
  ns, `std::int64_t` rate in parts-per-million, `bool` playing), publish methods
  `publishPosition(core::TimePosition, std::chrono::nanoseconds captured_at)`,
  `publishPlaying(bool)`, `publishRate(double)` — all noexcept, no allocation, no JUCE.

**Public-header impact.** Two new public headers; nothing else changes.

**Testing plan.** New `rock-hero-common/audio/tests/test_playback_clock.cpp` registered in
`rock-hero-common/audio/tests/CMakeLists.txt` (test includes the `src/` header via the existing
private include path):

- default snapshot is zero, stopped, rate 1.0, capture stamp 0;
- published position round-trips exactly through ns conversion at representative values
  (0, sub-ms, multi-minute);
- position, playing, and rate update independently (decision 6's documented incoherence);
- `static_assert` that every atomic member `is_always_lock_free`.

**Exit criteria.** Headers compile standalone; tests pass; no Tracktion include anywhere in the
new files.

**Verification** (new files → CMake source lists change → configure needed):

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\.agents\rockhero-build.ps1 -Configure -Targets all
powershell -NoProfile -ExecutionPolicy Bypass -File .\.agents\rockhero-build.ps1 -RunTouchedTests
```

### Phase 2 — Engine implements the clock with boundary publishes

**Scope.** `Engine` adds `IPlaybackClock` to its port bases
(`rock-hero-common/audio/include/rock_hero/common/audio/engine/engine.h`), following the existing
multi-port pattern. `Engine::Impl` (`src/engine/engine_impl.h`, declaration only) owns an
`AtomicPlaybackClock`. New per-port translation unit `src/engine/engine_clock.cpp` defines
`Engine::snapshot()` plus a private `publishClockBoundary(...)` helper, per the Multi-TU
coordination rules in docs/design/architectural-principles.md.

Boundary publish points (all message-thread, decision 7), wired where those operations already
live so publishes and listener notifications share one source of truth
(`Engine::Impl::currentTransportState()`):

- construction → position 0, stopped (`engine.cpp`);
- `setActiveArrangement` / `clearActiveArrangement` → reset position, stopped
  (`engine_song_audio.cpp`);
- `seek()` → the clamped position (`engine_transport.cpp`, reuse `clampToLoadedRange`);
- `play()` / `pause()` / `stop()` → playing flag from `currentTransportState()`, and position 0
  on `stop()` (`engine_transport.cpp`);
- auto end-of-file stop → final stopped state (the `valueTreePropertyChanged` →
  `stopTransport()` path, `engine_transport.cpp:31-44`).

Boundary publishes stamp `monotonic_capture_time` with steady-clock now; `playback_rate` stays
1.0 (published once at construction).

**Public-header impact.** `engine.h` gains one include, one base class, and one override; no other
public surface changes.

**Testing plan.** Extend `rock-hero-common/audio/tests/test_engine.cpp`, mirroring the existing
headless adapter patterns (e.g. "Engine seek updates current transport position",
`test_engine.cpp:840`):

- construction exposes a zero, stopped snapshot with rate 1.0;
- loading an arrangement leaves the clock stopped at the start;
- seek publishes the clamped position (in-range, negative, and beyond-length cases);
- after each of play/pause/stop, `snapshot().playing == state().playing` (consistency with the
  listener-facing coarse state, robust to headless transport behavior);
- stop publishes position zero;
- every publish updates `monotonic_capture_time` to a non-zero, non-decreasing stamp.

**Exit criteria.** All new and existing audio tests pass; `snapshot()` is implemented purely over
`AtomicPlaybackClock` (no Tracktion traversal — enforced by review of `engine_clock.cpp`).

**Verification:**

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\.agents\rockhero-build.ps1 -Configure -Targets all
powershell -NoProfile -ExecutionPolicy Bypass -File .\.agents\rockhero-build.ps1 -RunTouchedTests
```

### Phase 3 — Audio-derived publishing during playback

**Scope.** While the transport is playing, the adapter republishes audible time into the clock at
render-adjacent cadence (decision 13). Implementation: a private republisher inside the Tracktion
adapter (`src/engine/engine_clock.cpp` plus, if a framework subclass is needed for the timer, its
own named unit under `src/tracktion/` per the Framework-Adapter Units rule):

- a message-thread ticker (juce::Timer at ~60 Hz, or VBlank-driven) active only while playing;
- each tick performs the lifetime-safe read `Engine::position()` already proves out
  (`engine_transport.cpp:148-164`): audible time via the playback context when available,
  transport position otherwise; stamps steady-clock now; publishes.

The tick body is extracted as a testable `Engine::Impl` step (`publishAudibleTimeNow()`) so tests
can drive it without a timer.

**Checkpoint (framework behavior):** before implementing, verify with
`.claude/agents/juce-tracktion-expert.md`: (1) whether Tracktion offers a supported public hook or
custom-node insertion point for per-audio-block position taps (the escalation path of decision
13), and (2) any lifetime caveats on `getCurrentPlaybackContext()` reads between
`freePlaybackContext()` and rebuilds beyond what `stopTransportAndReleaseContext()`
(`engine_transport.cpp:74-84`) already implies. Record findings in this plan's inventory when
executed. The v1 republisher does not depend on (1); (1) only prices the escalation.

**Public-header impact.** None.

**Testing plan.**

- Adapter test (extends `test_engine.cpp`, using the private-include path): after a seek,
  invoking the extracted publish step republishes the current position with a fresh capture
  stamp; with no playback context, the step publishes transport position without crashing.
- Publisher lifecycle test: play → ticker active; pause/stop → ticker stopped and final boundary
  value published (observable via snapshot after command).
- Live-device smoke (manual, editor): with an audio device, snapshot position advances during
  playback and tracks the audible cursor. Automated CI has no audio device; the automated
  coverage above plus plan 25's debug overlay carry the live validation.

**Exit criteria.** Snapshot advances during real playback (manual smoke); automated tests pass;
audio thread untouched (no new audio-callback code); no consumer-thread publishes.

**Verification:**

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\.agents\rockhero-build.ps1 -Targets all
powershell -NoProfile -ExecutionPolicy Bypass -File .\.agents\rockhero-build.ps1 -RunTouchedTests
```

### Phase 4 — PlaybackClockExtrapolator (consumer-side smoothing policy)

**Scope.** Pure, deterministic, allocation-free value class in
`rock-hero-common/audio/include/rock_hero/common/audio/clock/playback_clock_extrapolator.h` (+
`src/clock/playback_clock_extrapolator.cpp`). It lives in common — not game — because plans 25
and 44 both consume it (constraint (a): shared code is extracted to common first), and in the
audio library because audio modules own audio-adjacent policy and it consumes
`PlaybackClockSnapshot`.

API sketch (time injected per "Time Must Be a Dependency"; one instance per consumer thread, no
internal synchronization — document this):

```cpp
struct PlaybackClockExtrapolationPolicy
{
    double max_correction_rate{0.05};              // drift slew: fraction of elapsed frame time
    core::TimeDuration snap_threshold{0.120};      // |target - output| beyond this snaps
};

class PlaybackClockExtrapolator
{
public:
    explicit PlaybackClockExtrapolator(PlaybackClockExtrapolationPolicy policy = {});
    // Consumes the latest snapshot and the consumer's monotonic now; returns smoothed time.
    [[nodiscard]] core::TimePosition advance(
        const PlaybackClockSnapshot& snapshot, std::chrono::nanoseconds monotonic_now);
    void reset() noexcept;
};
```

Policy rules (the game-side extrapolation policy, normative):

- **Target**: while playing, `target = snapshot.position + (now - capture_time) x rate`, elapsed
  clamped non-negative; capture stamp 0 (never published) → target = snapshot.position.
- **Paused/stopped**: output = snapshot.position exactly (immediate snap, no smoothing). A seek
  while paused therefore lands exactly.
- **Snap rules**: snap to target on (1) first call, (2) playing-state transition (start/resume),
  (3) |target - previous output| > snap_threshold — which covers seeks, practice-mode loop wraps
  (backward jumps are legitimate, from docs/plans/28-practice-mode.md), and pathological stalls.
- **Drift correction**: otherwise, advance by `frame_delta x rate` and slew the residual toward
  target, clamped to `± max_correction_rate x frame_delta`. Output never moves backwards during
  continuous playback (regression clamps to hold; only snap rules may rewind).
- **Rate**: uses the snapshot's `playback_rate` (1.0 until plan 21 publishes otherwise); a rate
  change mid-playback flows through the same slew/snap rules.
- **Publisher stalls**: with no fresh snapshot, target keeps advancing from the stale capture
  stamp — output stays smooth; accumulated error is bounded by audio-clock vs steady-clock skew.

**Public-header impact.** One new public header (+ policy struct within it).

**Testing plan.** New pure test file `rock-hero-common/audio/tests/test_playback_clock_extrapolator.cpp`
(synthetic snapshots and synthetic now — no Engine, no JUCE runtime):

- steady 60 Hz frames against 20 Hz publishes at rate 1.0 → output monotonic, within epsilon of
  ideal;
- jittered publish stamps → output monotonic, error bounded;
- pause → exact hold; resume → snap to target;
- forward and backward seeks beyond threshold → snap; loop-wrap backward jump → snap;
- rates 0.75 and 1.25 → advance rate honored (plan 28 plumbing proven);
- injected small drift → correction per frame never exceeds `max_correction_rate x frame_delta`;
- publisher stall (no new snapshots for 500 ms of frames) → smooth continued advance.

**Exit criteria.** All tests pass; the class has zero framework includes; defaults match open
question 3's proposal (or the user's revision).

**Verification:**

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\.agents\rockhero-build.ps1 -Configure -Targets all
powershell -NoProfile -ExecutionPolicy Bypass -File .\.agents\rockhero-build.ps1 -RunTouchedTests
```

### Phase 5 — Consumer handoff notes (documentation-only closeout)

**Scope.** No code. Record in this plan (and mirror in docs/plans/00-roadmap.md status) the
consumer contract for downstream plans:

- Use `IPlaybackClock` for render-frame highway position (25/44), scoring-time lookup (24),
  calibration sampling (13), and debug timing overlays (20). Use `ITransport` for commands and
  message-thread UI state. Use timeline/session state for duration and ranges. Never infer
  duration from the clock; never drive command behavior from the clock when message-thread state
  is available.
- One `PlaybackClockExtrapolator` per consumer thread; scoring (24) consumes snapshots/chart-time
  correlation, never extrapolated frame time (decision 11).
- `ITransport::position()` remains the editor's message-thread path pending open question 2; its
  retirement is re-evaluated in docs/plans/44-editor-3d-preview.md.

**Exit criteria.** Roadmap status updated; open questions 1-3 answered or explicitly carried in
docs/plans/00-roadmap.md "Decisions needed".

## Final acceptance phase

Run the sanctioned bundle from the repository root as separate invocations (constraint (h)), after
all phases land:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\.agents\rockhero-build.ps1 -Targets all
powershell -NoProfile -ExecutionPolicy Bypass -File .\.agents\rockhero-build.ps1 -RunTouchedTests
powershell -NoProfile -ExecutionPolicy Bypass -File .\.agents\rockhero-build.ps1 -Targets clang-tidy
pre-commit run --all-files
```

Acceptance: all four green; new public headers documented per
docs/design/documentation-conventions.md; no Tracktion include outside `rock-hero-common/audio`
implementation files; `snapshot()` and the extrapolator verified allocation- and lock-free by
review; commits on `refactor` with short imperative subjects.

## Rollback/abort notes

- **Phases 1, 2, 4 are additive and low-risk**: new headers, one new Engine base, new tests.
  Rollback is a plain `git revert` of the phase commits; no file formats, settings, or persisted
  state are touched anywhere in this plan.
- **Phase 3 is the risk phase.** The v1 republisher deliberately avoids the two dangerous paths
  (audio-callback code and a Tracktion submodule patch). If the juce-tracktion-expert checkpoint
  finds the playback-context read has lifetime caveats the current `Engine::position()` pattern
  does not already cover, fall back to publishing `transport.getPosition()` values (coarser but
  safe) and record the limitation here; consumers are unaffected because the API and capture-stamp
  semantics are unchanged.
- **Escalation guard**: do not attempt the audio-graph tap node or any submodule patch inside this
  plan. If plan 25's measured render telemetry proves message-thread republish cadence
  insufficient, open the escalation as its own registered follow-up in docs/plans/00-roadmap.md
  with the expert findings attached — the public API guarantees consumers never notice the swap.
- **Abort condition**: if headless adapter tests reveal that boundary publishes cannot mirror
  `currentTransportState()` deterministically without a device, keep Phase 2's publishes wired to
  command intent instead (publish what the command requested), note the divergence in this plan,
  and keep the listener surface as the sole Tracktion-truth reporter — consumers still get
  correct values once Phase 3's playback republisher runs.
