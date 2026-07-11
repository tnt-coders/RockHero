# Plan 21 — Game Audio Engine and GameplaySession

Status: Decision-gated (Phase 0 sign-off; render-loop coexistence input from
docs/roadmap/20-game-architecture-and-render-stack.md Phase 0b). Date: 2026-07-06.
Baseline: `refactor @ 3c7febe0`.

## Goal

The game plays a song end to end through the same audio engine that authored it: load a `.rock`
package, hear the FLAC backing track (normalization gain and start offset honored), play a real
guitar live through the arrangement's tone rig with the tone switching automatically at tone-track
region boundaries, and expose one authoritative playback clock to gameplay. A player can start,
pause, restart instantly, and finish a song with no audio dropouts and no tone drift from what the
charter authored in the editor.

## Non-goals

- Note detection, scoring, or any DSP on the guitar signal
  (docs/roadmap/22-note-detection.md, docs/roadmap/24-scoring-star-power-failure.md).
- Rendering, menus, or song-library UX (docs/roadmap/20-game-architecture-and-render-stack.md,
  docs/roadmap/25-note-highway-3d.md, docs/roadmap/26-game-startup-menus-library.md).
- Shipping playback speeds other than 1.0 or user-facing practice loops — this plan only plumbs
  the speed factor and loop-region seek through the interfaces so
  docs/roadmap/28-practice-mode.md is not a rewrite.
- The playback-clock design itself (docs/roadmap/12-playback-clock.md owns IPlaybackClock; this
  plan consumes it).
- Audio device selection/calibration UX (docs/roadmap/13-audio-device-settings-and-calibration.md).
- Editor tone-authoring work; docs/in-progress/tone-track-tempo-map-plan.md slice 5 is active
  editor work that this plan references and must not duplicate or modify.

## Constraints

Before starting any phase, work through the render-stack watch list in
`docs/todo/game-render-watch-items.md` — item 1 (GameShell composition placement) is explicitly
gated on this plan's start and must be decided, not accreted past.

Applicable subset of the roadmap's non-negotiable block:

- (a) **Layering**: common never depends on editor or game code; editor and game never depend on
  each other. Anything both products need is extracted to rock-hero-common FIRST — as its own
  phase with tests — before game code consumes it. Game code never includes editor headers.
  Tracktion headers stay isolated to rock-hero-common/audio implementation files.
- (b) **Public-header minimalism**: only headers that must be public are public;
  ports-and-adapters per docs/design/architectural-principles.md ("Ports and Adapters").
- (c) **NAMING FIREWALL**: the commercial real-guitar game that inspired this project is never
  named; use "RS" or neutral phrasing.
- (e) **FLAC** is the enforced package audio format.
- (g) **Tone fidelity**: tone documents are authored against the Tracktion rack path (multi-tone
  rack, Tracktion-managed plugin state). In-game tone reproduction must use the same engine path,
  or this plan must prove bit-equivalent output — a from-scratch game audio path would silently
  break every charted tone.
- (h) **Builds**: all build/test/lint commands go through `.agents/rockhero-build.ps1` (usage in
  `.agents/README.md`) — never raw cmake/ctest/ninja. Intermediate phases run only the checks
  their changes determinately warrant. The final acceptance phase is the sanctioned bundle as
  separate invocations.
- (i) **Real guitar input** — no plastic-controller assumptions; the live signal path is a real
  instrument through an arbitrary VST chain.

Additional binding design-doc rules:

- The audio thread is the single source of truth for timing; no second clock
  (docs/design/architecture.md, "Timing and Latency" and "Threading Model").
- The game treats the `Song` model as read-only during gameplay
  (docs/design/architecture.md, "Application Responsibilities"); the game never rewrites user
  packages (roadmap tension 7).
- Game-side orchestration policy belongs in `rock-hero-game/core`, headless and testable with
  fakes (docs/design/architectural-principles.md, "Library Roles" and "Add a Replayable
  Simulation Layer").

## Current state inventory

- `common::audio::Engine` is the single Tracktion isolation layer and already implements every
  port the game needs to start: `ITransport`, `ISongAudio`, `IAudioDeviceConfiguration`,
  `IAudioMeterSource`, `IPluginHost`, `ILiveInput`, `ILiveRig`, `IThumbnailFactory`
  (rock-hero-common/audio/include/rock_hero/common/audio/engine/engine.h:57-64). Most public
  methods are message-thread-only; plugin catalog scans are the explicit worker-thread exception
  (engine.h header comment). The engine therefore requires a running JUCE message loop wherever
  it is embedded.
- `ITransport` is play/pause/stop/seek plus message-thread `state()`/`position()` reads and a
  coarse state listener (rock-hero-common/audio/include/rock_hero/common/audio/transport/
  i_transport.h). `TransportState` is `{bool playing}` (transport/transport_state.h:12-24).
  There is **no playback-speed and no loop-region API anywhere in common/audio public headers**
  (`rg -n "speed|loop" rock-hero-common/audio/include` shows only message-loop doc prose).
- `ISongAudio::setActiveArrangement` already honors the two package audio facts gameplay needs:
  a positive `AudioAsset.start_offset` places the backing clip late with silence before it
  (rock-hero-common/audio/src/engine/engine_song_audio.cpp:118-126), and persisted normalization
  gain is applied via `wave_clip->setGainDB` (engine_song_audio.cpp:156-160). It also sets
  `transport.looping = false` (engine_song_audio.cpp:163) — the Tracktion transport looping flag
  exists and is already touched. The backing clip deliberately disables Tracktion's proxy
  because "Practice-speed playback time-stretches this clip live … responds to speed changes
  immediately" (engine_song_audio.cpp:142-154) — the engine is already positioned for live
  time-stretch through WaveNodeRealTime's elastique reader.
- `ILiveRig::loadLiveRig` already preloads **multiple** tone documents: `LiveRigLoadRequest`
  carries `tone_document_refs` where "Every referenced tone loads into its own always-processing
  branch of the multi-tone rig so switching between them never rebuilds plugins", and
  `setAudibleTone` switches branches through short click-free ramps — documented as "the
  selection-driven switch path; scheduled playback switching is baked separately"
  (rock-hero-common/audio/include/rock_hero/common/audio/live_rig/i_live_rig.h:98-131, 199-210).
- The multi-tone rack backend exists: `buildToneRack` assembles one `RackType` with a parallel
  branch per tone, each terminated by a `ToneBranchGainPlugin` whose "automation curve carries
  the region schedule" (rock-hero-common/audio/src/tracktion/multi_tone_rack.h:27-78,
  tone_branch_gain_plugin.h/.cpp). **No schedule-baking port exists yet**: `rg` finds no
  `IToneTimelinePlayer`/`prepareToneTimeline` anywhere in the tree — that is editor slice 5c,
  in flight per docs/in-progress/tone-track-tempo-map-plan.md.
- The tone data model is in common/core: `Tone` (named catalog entry keyed by
  `tone_document_ref`), `ToneRegion` (musical `start`/`end`, tone ref), `ToneTrack` (sorted,
  non-overlapping regions; "Gaps are allowed; playback holds the previous region's tone through
  a gap") — rock-hero-common/core/include/rock_hero/common/core/tone/tone_track.h.
  `Arrangement` carries `tone_document_ref` (whole-song default), the `tones` catalog, the
  `tone_track`, `chart_ref`, and optional loaded `Chart`
  (rock-hero-common/core/include/rock_hero/common/core/song/arrangement.h:39-121).
- Package IO: `readRockSongPackage(package_path, workspace_directory)` extracts a `.rock` into
  an existing workspace directory and parses it; a directory-based reader and writers also
  exist (rock-hero-common/core/include/rock_hero/common/core/package/rock_song_package.h).
  The reader hard-rejects `formatVersion != 1` (rock-hero-common/core/src/package/
  rock_song_package_read.cpp, ~line 976) — game-side version tolerance is
  docs/roadmap/10-format-versioning-and-chart-identity.md scope.
- `rock-hero-game` is a build skeleton: app/main.cpp is an 81-line JUCE `DocumentWindow` shell
  (a full JUCE application with a running message loop), and core/audio/ui are `placeholder.cpp`
  static libraries. No game tests exist. SDL3/bgfx appear nowhere in the build.
- Adapter test infrastructure exists at rock-hero-common/audio/tests/test_engine.cpp with a
  `drum_loop.wav` fixture wired through CMake (tests/CMakeLists.txt:6-7).
- docs/todo/audio-engine-multi-track-support.md predates the current engine surface (it cites
  `Engine::createTrack()`/`IEdit`/`EditCoordinator`, none of which exist today) — noted for the
  roadmap disposition table, not absorbed here.

Verified against code on 2026-07-06, refactor @ 3c7febe0.

## Dependencies

- docs/roadmap/20-game-architecture-and-render-stack.md — Phase 0b (JUCE MessageManager/Tracktion
  coexistence with the chosen game main loop). Gates only this plan's **final integration**
  shape; Phases 1-6 here run inside the existing JUCE game shell, which already owns a message
  loop.
- docs/roadmap/12-playback-clock.md — its IPlaybackClock introduction phase. Phase 2 designs the
  GameplaySession to hand IPlaybackClock to gameplay consumers; until plan 12 lands, the session
  exposes message-thread `ITransport::position()` behind the same accessor seam.
- docs/roadmap/13-audio-device-settings-and-calibration.md — shared per-device-identity settings
  store; Phase 6 uses fixed devices until it lands.
- docs/roadmap/22-note-detection.md — Phase 1 detection contract defines the dry-signal tap point;
  this plan's Phase 5 records where the dry tap must sit (before the tone rack) so the two plans
  agree on one seam.
- docs/roadmap/26-game-startup-menus-library.md — supplies real song selection; Phase 6 here
  hardcodes one package path.
- docs/roadmap/27-in-song-flow-results-profiles.md — IGameSettings store persists Phase 4's mix
  values; until then they are session-local.
- docs/roadmap/28-practice-mode.md — consumer of the speed factor and loop-region plumbing from
  Phase 1; its time-stretch quality verification does not block this plan.
- docs/roadmap/47-editor-loop-selection.md — declares the same shared-port loop surface under a
  whichever-executes-first rule (its Decisions) and is expected to execute first, landing the
  Tracktion-backed loop; Phase 1 here then re-verifies the landed surface and adds only the
  speed methods.
- docs/in-progress/tone-track-tempo-map-plan.md — slice 5 (runtime switching) is active editor
  work defining the schedule-baking mechanism and its verified Tracktion facts. Reference only;
  Phase 3 coordinates with it instead of duplicating it.

## Decisions already made

Restated inline so a fresh session needs no other context:

- **Both executables embed `common::audio` (Tracktion) as their audio engine.** The architecture
  diagram places "common/audio (Tracktion Engine)" inside the game box, and static linking of
  `rock_hero::common + rock_hero::game` is a reasoned decision
  (docs/design/architecture.md, "Architecture Diagram").
- **Tone switching is preloaded multi-tone rack + branch-gain automation evaluated by the audio
  thread against the transport; no external position pushes** — a second clock is forbidden
  (docs/in-progress/tone-track-tempo-map-plan.md, Decisions 5-7; docs/design/architecture.md,
  "Timing and Latency"). Automation evaluates once per audio block, so switch onsets quantize to
  the block (~3-10 ms) with per-sample smoothing keeping transitions click-free — that meets the
  product bar; do not chase sample accuracy (same doc, Decision 7).
- **Seek resync is automatic**: parameter streams follow the transport position while stopped or
  scrubbing, so branch gains snap to the playhead without an explicit position push
  (docs/in-progress/tone-track-tempo-map-plan.md, verified mechanism notes, citing
  plugins/tracktion_Plugin.cpp:676 in the vendored engine).
- **Latency compensation should be OFF for the live path**: `Edit::setLatencyCompensationEnabled
  (false)` is a public API (vendored tracktion_Edit.h:539); with PDC on, rack monitoring latency
  equals the worst branch at all times; with it off, latency equals the active branch only
  (docs/in-progress/tone-track-tempo-map-plan.md, latency amendment 2026-07-05).
- **Gap behavior**: a tone-track gap holds the previous region's tone; before the first region
  the arrangement default tone (`tone_document_ref`) is audible
  (rock-hero-common/core/include/rock_hero/common/core/tone/tone_track.h ToneTrack docblock;
  docs/in-progress/tone-track-tempo-map-plan.md, Validation).
- **FLAC is enforced by the package reader**; audio entering in other formats was transcoded on
  import (docs/design/architecture.md, "Technology Stack").
- **The game never mutates user packages** (docs/design/architecture.md, "Application
  Responsibilities": the game "Treats the `Song` model as read-only during gameplay").
- **Pre-activated silent plugins are the accepted CPU tradeoff**; do not design tone-count caps
  now (docs/in-progress/tone-track-tempo-map-plan.md, preloading tradeoff note; matches
  docs/design/architecture.md "VST Plugin Safety" pre-activation guidance).

## Open questions for the user

1. **Missing-plugin fallback policy.** A tone document may reference VSTs not installed on the
   player's machine. Options: (A) refuse to start the song, listing the missing plugins;
   (B) load the chain with missing plugins skipped, show a pre-song warning naming them, and
   play with the partial tone; (C) substitute a bundled default clean tone for any tone that
   fails entirely. **Recommendation: B, with C as the degraded case when an entire tone chain
   fails to load — never block gameplay on tone fidelity, never fail silently.** The warning
   should also mark the run so results can record "tone degraded" (consumed by
   docs/roadmap/24-scoring-star-power-failure.md's score record).
2. **Per-tone reported-latency policy for live monitoring.** Tone docs are arbitrary VST chains
   with unbounded reported latency (a lookahead limiter can report 100+ ms). Options: (A) warn
   pre-song when any tone's summed reported latency exceeds a threshold (suggest 10 ms), play
   anyway; (B) hard-refuse tones above a cap; (C) silent. **Recommendation: A — warn-only.**
   With compensation off the player only suffers the active branch's real latency, and scoring
   is unaffected because detection taps the dry input (plan 22); a hard cap would break
   legitimately authored tones.
3. **Mix-volume scope.** Should master/backing/monitor volumes be global settings, per-song, or
   both? **Recommendation: global in v1 (persisted via docs/roadmap/27-in-song-flow-results-
   profiles.md's IGameSettings), with per-song override deferred until requested.**

## Phased implementation

### Phase 0 — Tracktion-in-game go/no-go (decision gate)

Scope: confirm the forced default rather than spike an alternative. Constraint (g) means a
from-scratch game audio path is acceptable only with proof of bit-equivalent output through
arbitrary third-party VST chains — practically unattainable, since plugin state restore, rack
topology, branch-gain ramps, and block-boundary automation timing would all have to match the
Tracktion path exactly. The candidate options are:

- **Embed `common::audio::Engine` in the game (recommended).** Costs: the game must run a JUCE
  message loop wherever the engine lives (already true today — the game shell is a JUCE app),
  and plan 20 Phase 0b must prove coexistence if an SDL-owned main loop is chosen. Benefits:
  tone documents, normalization, start offsets, device handling, and the multi-tone rack are
  reused byte-for-byte identical to the editor's audition path; the fallback-to-raw-JUCE
  strategy stays intact behind the same ports (docs/design/architecture.md, "Fallback
  Strategy").
- **From-scratch game audio path.** Rejected unless the user overrides: violates constraint (g)
  without a bit-equivalence proof; silently breaks every charted tone.

Steps: none beyond presenting this analysis together with plan 20 Phase 0b's findings.
Exit criteria: **STOP — present findings and get user sign-off.** All later phases assume
outcome "embed the common Engine" (GO).

Verification commands: none (documentation-only gate).

### Phase 1 — Speed factor and loop-region plumbing in the shared transport port (assumes GO)

Scope: extend `ITransport` (rock-hero-common/audio/include/rock_hero/common/audio/transport/
i_transport.h) and `Engine` with the two gameplay-shaped controls that must exist from day one
so docs/roadmap/28-practice-mode.md is additive, not a rewrite:

- `setPlaybackSpeed(double factor)` / `playbackSpeed()`: v1 accepts exactly 1.0; any other value
  returns a typed `TransportError{SpeedNotSupported}` (new small error enum in transport/).
  The eventual non-1.0 implementation rides the already-proxy-off backing clip
  (engine_song_audio.cpp:142-154 chose proxy-off precisely so elastique responds to live speed
  changes) — but the concrete Tracktion varispeed facility carries an explicit **"verify with
  juce-tracktion-expert before implementing"** checkpoint, executed by plan 28, not here.
- `setLoopRegion(common::core::TimeRange)` / `clearLoopRegion()`: implemented for real in this
  phase via Tracktion transport looping (the `transport.looping` flag is already manipulated at
  engine_song_audio.cpp:163; the loop-range API needs a **juce-tracktion-expert verification**
  of `TransportControl` loop-range semantics before coding). Loop wrap is a transport-internal
  jump, so tone-branch automation resyncs by the same mechanism as any seek (verified seek-
  resync fact in Decisions above). Coordination: docs/roadmap/47-editor-loop-selection.md Phase 1
  shares this exact surface (whichever-executes-first rule) and is expected to land it first —
  if it has, re-verify the landed loop and add only the speed surface, never re-implement.

Extending the shared port (rather than a game-only port) is deliberate: the editor already
anticipates practice-speed playback (the engine comment above), transport semantics belong on
the transport concept, and `Engine` is the single implementer today. Non-consumers ignore the
new methods.

Files/modules touched: transport/i_transport.h, new transport/transport_error.h,
engine/engine.h, src/engine/engine_transport.cpp, rock-hero-common/audio/tests/test_engine.cpp.
Public-header impact: two new methods plus one small error header in common/audio — reviewed
against constraint (b); no Tracktion types leak.

Testing plan (rock-hero-common/audio/tests, existing target, `drum_loop.wav` fixture):
loop-region set/clear round-trip through the port; playback confined to the loop range;
non-1.0 speed returns the typed error; speed 1.0 is accepted and reported. These prove the
plumbing exists and fails loudly rather than silently for unsupported values.

Exit criteria: tests green; editor build unaffected.
Verification commands:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\.agents\rockhero-build.ps1 -Targets all
powershell -NoProfile -ExecutionPolicy Bypass -File .\.agents\rockhero-build.ps1 -RunTouchedTests
powershell -NoProfile -ExecutionPolicy Bypass -File .\.agents\rockhero-build.ps1 -Targets clang-tidy
```

### Phase 2 — GameplaySession spine in rock-hero-game/core (assumes GO)

Scope: the headless "play a song" orchestration no other plan owns. A `GameplaySession` state
machine in `rock-hero-game/core` (feature folder `session/`), consuming only project-owned
ports (constraint (b); docs/design/architectural-principles.md "Ports and Adapters"):

- States: `Idle → Loading → PreparingRig → Ready → Playing ⇄ Paused → Finished`, plus a
  typed `Failed` terminal per stage. Store the stage's action inside each state so illegal
  states cannot exist (established editor-core pattern).
- `Loading`: extract the `.rock` via `readRockSongPackage` into a **per-session scratch
  workspace under per-user app data** (never next to the package; deleted on session close),
  select the arrangement, run `ISongAudio::prepareSong` + `setActiveArrangement`. The package
  file itself is never written (read-only game rule). Absent normalization metadata plays at
  0 dB in v1; docs/roadmap/26-game-startup-menus-library.md's cache may add game-side analysis
  later.
- `PreparingRig`: preload **every** tone referenced by the arrangement (deduped
  `tone_track.regions[].tone_document_ref` plus the default `tone_document_ref`) through
  `ILiveRig::loadLiveRig`, then hand the region schedule to the tone-timeline seam (Phase 3).
  The session must not report `Ready` until the completion callback fires — this is the
  pre-song preload guarantee: no plugin instantiation after `Ready`.
- `Ready/Playing`: `play()`, `pause()`, `seek`, instant restart (= `seek(start)` + `play()`,
  no rig teardown), and pass-throughs for speed/loop (Phase 1 surface) so plan 28 later drives
  them. Exposes the playback clock: IPlaybackClock once
  docs/roadmap/12-playback-clock.md lands; a thin message-thread `ITransport::position()`
  accessor behind the same session getter until then.
- All failures surface as typed session errors (docs/design/architectural-principles.md,
  "Typed Boundary Errors").

Files/modules touched: new rock-hero-game/core/include/rock_hero/game/core/session/*.h,
rock-hero-game/core/src/session/*.cpp, new rock-hero-game/core/tests/ target (links
`rock_hero_game_core`, per the project rule that test targets link the library they test),
CMake target additions. Public-header impact: game/core session headers only; nothing added to
common.

Testing plan (rock-hero-game/core/tests, new): hand-written fakes for `ISongAudio`,
`ITransport`, `ILiveRig`, and the Phase 3 tone-timeline seam. Prove: full happy-path state
walk; `Ready` never precedes rig-load completion; unreadable package, prepare failure, and
rig failure each land in the right typed `Failed`; restart does not re-trigger preload;
pause/play round-trip; the session never calls a port from a state that forbids it.

Exit criteria: tests green with fakes only (no audio device, no Tracktion).
Verification commands (CMake graph changes → configure):

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\.agents\rockhero-build.ps1 -Configure -Targets all
powershell -NoProfile -ExecutionPolicy Bypass -File .\.agents\rockhero-build.ps1 -RunTouchedTests
powershell -NoProfile -ExecutionPolicy Bypass -File .\.agents\rockhero-build.ps1 -Targets clang-tidy
```

### Phase 3 — Scheduled tone switching for gameplay (assumes GO; coordinates with editor slice 5)

Scope: give the game the position-driven region schedule over the already-existing multi-tone
rack, verifying fit of the editor mechanism instead of assuming it:

- **3a — shared conversion extraction (constraint (a) extraction-first).** The pure
  regions-to-seconds conversion (ToneRegion musical endpoints → `TimeRange` via
  `common::core::TempoMap`, gap-hold expansion, default-tone head region) must live in
  `rock-hero-common/core` (tone/ feature) with unit tests, because both products need it.
  Editor slice 5c currently plans this conversion in editor/core
  (docs/in-progress/tone-track-tempo-map-plan.md, sub-phase 5c). Coordination rule: if 5c has
  landed it editor-side by execution time, this phase **promotes** it to common/core and
  repoints the editor (a mechanical move, done here, with the editor building green before and
  after); if 5c has not landed, land it in common/core directly and tell the editor work to
  consume it. Do not edit in-flight editor tone files without that coordination.
- **3b — the tone-timeline port.** Adopt the `IToneTimelinePlayer` shape from the in-progress
  plan (prepare = build rack + preload + bake branch-gain curves; no per-frame calls; seek
  resync automatic). If the editor work has already created the port in common/audio, consume
  it unchanged; otherwise create it here per that plan's "Runtime Audio Direction" section.
  One port, both products — never two switching mechanisms.
- **3c — gameplay-fit verification.** The editor slice-5 mechanism bakes branch-gain automation
  into the edit's playback graph — an editor-playback-shaped mechanism. Verify the three
  game-specific behaviors against the real backend before accepting reuse: (1) instant restart
  (`seek(0)`+`play` with no rebuild, correct tone at position 0); (2) loop wrap via Phase 1's
  loop region (tone correct immediately after the wrap jump); (3) pre-song preload guarantee
  (no graph rebuild or plugin instantiation between `Ready` and the first note). Each is an
  adapter test in rock-hero-common/audio/tests where drivable, plus a listening check in
  Phase 6. If any fails, the named fallback is hidden parallel AudioTracks per the adversarial
  review in docs/in-progress/tone-track-tempo-map-plan.md — same switching model, different
  host topology; escalate to the user before switching backends.
- **3d — missing-plugin fallback** per the answer to open question 1, implemented at rig-load
  time with a typed report the session surfaces to UI (list of missing plugins per tone).

Files/modules touched: rock-hero-common/core tone/ (conversion + tests), common/audio
tone-timeline port + engine TU + adapter tests, game/core session wiring + fake-based tests.
Public-header impact: one new common/audio port header; one new common/core tone header.

Testing plan: pure conversion tests in rock-hero-common/core/tests (gap-hold, head region,
terminal clamping, empty tone track ⇒ single default region); adapter tests for 3c above;
game/core fake tests asserting the session passes the schedule exactly once per load.

Exit criteria: 3c's three behaviors demonstrably hold; editor still green.
Verification commands: same three invocations as Phase 2 (configure only if targets changed).

### Phase 4 — Mix controls (assumes GO)

Scope: master, backing-track, and player-monitor volumes as a small common/audio port
(`IMixControls` or an extension of existing ports — decided at implementation against
constraint (b)):

- Player monitor: already exists as `ILiveRig::outputGain/setOutputGain` — reuse, do not
  duplicate.
- Backing volume: a track-level gain that **composes with** (never overwrites) the
  normalization clip gain set at engine_song_audio.cpp:156-160.
- Master: the edit's master volume facility — **verify with juce-tracktion-expert** which
  Tracktion master-plugin surface is correct before implementing.

Game side: session-local values in v1; persistence lands with
docs/roadmap/27-in-song-flow-results-profiles.md's IGameSettings (record the key names there).
Files: common/audio port + engine TU + adapter tests; game/core session accessors + fake tests.
Public-header impact: one small port surface in common/audio.
Testing plan: adapter tests prove backing gain composes with normalization (set both, read
effective clip/track gains); fake tests prove the session round-trips values.
Exit criteria: volumes audibly independent in the Phase 6 soak.
Verification commands: same three invocations as Phase 1.

### Phase 5 — Live-monitoring latency stance (assumes GO)

Scope: make the latency policy executable, not folklore:

- Disable plugin-delay compensation on the game edit via
  `Edit::setLatencyCompensationEnabled(false)` (public API, vendored tracktion_Edit.h:539, per
  the in-progress plan's verified notes). PDC delays are poison for a live player: with PDC on,
  every branch of the multi-tone rack is delayed to the worst branch's reported latency at all
  times. With it off, the player hears only the active branch's real latency; branch mismatch
  matters only during the 5-10 ms crossfade (brief phase smear, not a timing error).
- Surface per-tone summed reported latency (chain `getLatencySeconds()` walk) through the
  rig-load result, and implement the warning policy per open question 2's answer.
- Record the dry-tap contract: detection (docs/roadmap/22-note-detection.md) taps the raw input
  **before** the tone rack (docs/design/architecture.md "Threading Model" already specifies the
  pre-effects ring-buffer copy), so chain latency never contaminates scoring timestamps. This
  phase adds the documentation and the seam assertion, not the tap itself.

Files: common/audio engine TUs (edit configuration + latency surfacing), i_live_rig.h result
struct extension, adapter tests. Public-header impact: latency fields on an existing result
struct. Testing plan: adapter test asserting compensation is off on the constructed edit and
that a known-latency test plugin's report is surfaced. Exit criteria: tests green; stance
documented in the port docblocks.
Verification commands: same three invocations as Phase 1.

### Phase 6 — Hardcoded-song playthrough soak (assumes GO; milestone 0 audio path)

Scope: wire `GameplaySession` into the existing JUCE game shell (rock-hero-game/app/main.cpp)
behind a dev path: hardcoded package path (command line or dev config), fixed audio devices,
keyboard start — the audio half of the roadmap's milestone 0 (docs/roadmap/00-roadmap.md). No
rendering dependency: this runs before plan 20's gate closes because the shell already owns a
JUCE message loop. Manual soak checklist: backing plays with normalization gain and
start-offset alignment; live guitar audible through the authored tone; tones switch at region
boundaries during play; instant restart and mid-song seek land on the correct tone; pause/
resume clean; mix controls act independently; a package with a missing plugin follows the
agreed fallback. Local-only corpus spot-checks (a handful of the 39 converted packages) —
never in CI, never committed.

Files: rock-hero-game/app/main.cpp plus a thin app-side composition unit; no new public
headers. Testing plan: this phase is deliberately manual/soak; automated coverage lives in
Phases 1-5. Exit criteria: full playthrough of one real song with zero dropouts and correct
tone changes, witnessed and noted in the roadmap status.
Verification commands:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\.agents\rockhero-build.ps1 -Targets all
```

then run the game executable from `build/debug` manually with the dev song path.

## Final acceptance phase

Per constraint (h), as separate invocations, all green:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\.agents\rockhero-build.ps1 -Targets all
powershell -NoProfile -ExecutionPolicy Bypass -File .\.agents\rockhero-build.ps1 -RunTouchedTests
powershell -NoProfile -ExecutionPolicy Bypass -File .\.agents\rockhero-build.ps1 -Targets clang-tidy
pre-commit run --all-files
```

Plus the Phase 6 manual soak checklist signed off, and the roadmap
(docs/roadmap/00-roadmap.md) status line for this plan updated.

## Rollback/abort notes

- **Phase 1** is additive to a public port; if the loop-range Tracktion verification fails,
  ship the port with loop returning a typed `NotSupported` (mirroring speed) so downstream
  plans still compile against the final surface — never remove the methods once published.
- **Phase 3a promotion**: if moving the conversion collides with in-flight editor tone work,
  abort the move, land a common/core copy marked as the canonical one, and file the editor
  repointing as an immediate follow-up — never leave two diverging implementations across a
  release boundary.
- **Phase 3c failure** (rack schedule unfit for restart/loop/preload): stop, present measured
  evidence, and get sign-off before adopting the hidden-parallel-AudioTracks fallback; that
  swap changes editor slice 5 too, so it is a joint decision with the in-progress plan's owner,
  not a local fix.
- **Phase 5**: if disabling compensation exposes an audible artifact in a real tone set,
  re-enable it behind the port and re-open open question 2 with the recording — the stance is
  policy, and policy changes go back to the user.
- **Phase 6** touches only app composition; rollback is deleting the dev entry point. Keep the
  dev song path out of committed default configs so a rollback never strands a hardcoded local
  path.
