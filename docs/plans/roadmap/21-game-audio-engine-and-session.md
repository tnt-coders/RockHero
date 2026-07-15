# Plan 21 — Game Audio Engine and GameplaySession

Status: **Phases 1–6 code-complete (Phases 1–3: 2026-07-11; Phases 4–6: 2026-07-12); Phase 6's
witnessed soak checklist AWAITS THE USER.** Phase 6 — session wired into the SDL shell per the
decided inject-from-app watch item (main.cpp owns the JUCE runtime, Engine, and GameplaySession;
the shell receives non-owning pointers and only drives them; plugin-scan child-process hook
mirrored from the editor), `--dev-package` now plays for real: Space toggles play/pause, R
instant-restarts, PgUp/PgDn seek the engine transport by section, the fixture's chosen
arrangement id feeds the session so display and audio always agree, and the real IPlaybackClock
drives the highway (stand-in clock only without a session). Automated soak evidence (scripted
smoke over a repackaged corpus song): session Loading→Ready→Playing via posted keys, song time
advancing on a ~7 ms-fresh clock mirror at 144 fps, restart exercised, clean exit; captures +
script in the session scratchpad. REMAINING for the user (witnessed checklist): backing
audibility/normalization/start-offset by ear, live guitar through the authored tone, audible
tone switches at region boundaries, pause/resume cleanliness, mix-control independence, and a
missing-plugin refusal spot-check. Stale "JUCE game shell" wording reconciled via the Phase 2
inventory correction. clang-tidy pending user trigger. Phase 5 —
latency stance: PDC disabled at edit construction for BOTH products (engine.cpp, full rationale
comment; aligned with the tone plan's 2026-07-05 latency amendment — one live path + backing
stem, active-branch-only latency); per-tone `summed_reported_latency_seconds` surfaced on
`LoadedToneChainIdentities` at load completion (consumer = the editor's authoring-time export
warning per the 21-Q2 refinement; gameplay silent); dry-tap contract recorded on `ILiveInput`
(detection taps pre-rack; chain latency never touches scoring timestamps). Coverage notes: no
latency-reporting plugin exists headless, so the adapter test pins the field at exactly 0.0 for
empty chains; the private edit cannot be reached for a direct PDC-off assertion — both are
source-verified one-liners whose live confirmation folds into Phase 6's soak (the plan-12/47
split). Verified `-Targets all` + `-RunTouchedTests` green; clang-tidy pending user trigger. Phase 4 — mix
controls: `IMixControls` port (common/audio mix/; master + backing only — monitor stays
`ILiveRig::outputGain`, one owner per volume), Engine implementation over expert-verified
Tracktion surfaces (`getMasterVolumePlugin()->setVolumeDb` — always-present plugin, -3 dB
fresh-edit default reported truthfully, master stage verified to cover live monitoring on the
default output device; `backingTrack()->getVolumePlugin()` — separate stage from clip
normalization, composition source-verified), session volume accessors forwarding to single
owners, mix keys reserved in plan 27 Phase 1 (`mixMasterDb`/`mixBackingDb`/`mixMonitorDb`).
Adapter tests: master default + round-trip, backing round-trip surviving a normalization-bearing
arrangement load; session fake test for the three-owner forwarding. Verified `-Targets all` +
`-RunTouchedTests` green; clang-tidy pending user trigger. Phase 3 — scheduled tone switching: 3a/3b
finalized (crossfade-envelope math `makeToneGainEnvelope` joined `makeToneSchedule` in
common/core with 4 more unit tests; `IToneTimelinePlayer` implemented on Engine in
src/engine/engine_tone_timeline.cpp — bake-once branch-gain curve writing over the loaded rack,
automation-read gate enforced, `setToneTimelinePosition` a documented no-op per the verified
auto-resync fact); 3c evidence: origin-point correctness unit-tested, wrap ≡ seek confirmed at
source by Phase 1's expert pass (verdict 5), no-rebuild-after-Ready structural (baking happens
inside prepare, before the session reports Ready) — audible confirmation stays in Phase 6's
soak; 3d per 21-Q1(A): rig load now scans to completion and refuses ONCE listing every
uninstalled plugin (new `LiveRigErrorCode::MissingPlugins`, collect-and-continue at both the
identity-resolution and restore-load-error sites), session maps it to
`GameplaySessionErrorCode::MissingPlugins` for the install-these UI. Editor coordination note
added to the in-progress tone plan (slice 5c consumes, never re-declares). Adapter tests:
timeline-requires-rig, bake/re-bake/empty/unknown-ref, missing-plugin aggregation (three fixture
iterations: canonical UUID doc path + canonical `.tracktion-plugin` sidecars that must exist).
Verified `-Configure -Targets all` + `-RunTouchedTests` green; clang-tidy pending user trigger. Phase 1 — speed + loop surface landed on
`ITransport`/`Engine` under the whichever-executes-first rule with plan 47 (which had not
executed; the FULL loop surface from its Phase 1 spec landed here — see the coordination banner
in that plan); expert source verification of `TransportControl` loop semantics preceded coding.
Phase 2 — `GameplaySession` state machine in rock-hero-game/core session/ (typed stages,
scratch-workspace package loading, editor-mirrored rig preload, pre-song preload guarantee,
instant restart, speed/loop pass-throughs, IPlaybackClock exposure; 10 fake-driven test cases);
pulled forward per the plans' own whichever-first wording: minimal Phase 3a conversion
(`makeToneSchedule` in common/core tone/, 5 unit tests) and the Phase 3b port declaration
(`IToneTimelinePlayer` in common/audio tone_timeline/, adopted shape) — 3c gameplay-fit
verification and 3d missing-plugin fallback remain Phase 3's substance. Both phases verified
with `-Targets all` + `-RunTouchedTests` green; clang-tidy deferred to a user-triggered run per
the on-demand-only rule (this plan's verification lists predate that 2026-07-09 rule).
**G21-TRACKTION-GO CLOSED — user signed off 2026-07-10: embed `common::audio::Engine` in
the game (Phase 0 outcome GO).** The coexistence input was already proven by plan 20's gate
(criterion S1: SDL-owned loop + JUCE message drain + engine playback). Phases 2–6 are unblocked;
plan 20 Phase 3's S1-mirror soak (window renders while the backing engine plays) transfers to
this plan's first engine-embedding phase, since that is the first moment an engine exists in the
game process. Date: 2026-07-06 (gate closed 2026-07-10). Original baseline: `refactor @
3c7febe0`; Phase 1 executed against `master @ 7ba93b90`.

## Goal

The game plays a song end to end through the same audio engine that authored it: load a `.rock`
package, hear the FLAC backing track (normalization gain and start offset honored), play a real
guitar live through the arrangement's tone rig with the tone switching automatically at tone-track
region boundaries, and expose one authoritative playback clock to gameplay. A player can start,
pause, restart instantly, and finish a song with no audio dropouts and no tone drift from what the
charter authored in the editor.

## Non-goals

- Note detection, scoring, or any DSP on the guitar signal
  (docs/plans/roadmap/22-note-detection.md, docs/plans/roadmap/24-scoring-star-power-failure.md).
- Rendering, menus, or song-library UX (docs/plans/roadmap/20-game-architecture-and-render-stack.md,
  docs/plans/roadmap/25-note-highway-3d.md, docs/plans/roadmap/26-game-startup-menus-library.md).
- Shipping playback speeds other than 1.0 or user-facing practice loops — this plan only plumbs
  the speed factor and loop-region seek through the interfaces so
  docs/plans/roadmap/28-practice-mode.md is not a rewrite.
- The playback-clock design itself (docs/plans/roadmap/12-playback-clock.md owns IPlaybackClock; this
  plan consumes it).
- Audio device selection/calibration UX (docs/plans/roadmap/13-audio-device-settings-and-calibration.md).
- Editor tone-authoring work; docs/plans/in-progress/tone-track-tempo-map-plan.md slice 5 is active
  editor work that this plan references and must not duplicate or modify.

## Constraints

Before starting any phase, work through the render-stack watch list in
`docs/tracking/watch-items.md` — the GameShell composition-placement entry is explicitly gated on
this plan's start and must be decided, not accreted past.

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
  port the game needs to start: `ITransport`, `IPlaybackClock` (landed with plan 12),
  `ISongAudio`, `IAudioDeviceConfiguration`, `IAudioMeterSource`, `IPluginHost`, `ILiveInput`,
  `ILiveRig`, `IToneAutomation`, `IThumbnailFactory`
  (rock-hero-common/audio/include/rock_hero/common/audio/engine/engine.h:61-70). Most public
  methods are message-thread-only; plugin catalog scans are the explicit worker-thread exception
  (engine.h header comment). The engine therefore requires a running JUCE message loop wherever
  it is embedded. Transport operations publish playback-clock boundaries
  (engine_transport.cpp: play/pause/seek/stop all call `publishClockBoundary`) — new transport
  operations that move the playhead must follow that pattern.
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
  in flight per docs/plans/in-progress/tone-track-tempo-map-plan.md.
- The tone data model is in common/core: `Tone` (named catalog entry keyed by
  `tone_document_ref`), `ToneRegion` (musical `start`/`end`, tone ref), `ToneTrack` (sorted,
  non-overlapping regions; "Gaps are allowed; playback holds the previous region's tone through
  a gap") — rock-hero-common/core/include/rock_hero/common/core/tone/tone_track.h.
  **Correction 2026-07-11 (lean tone format, tone-capture-scope work): `Arrangement` no longer
  carries a whole-song default `tone_document_ref` field** — it carries the `tones` catalog, the
  `tone_track`, `chart_ref`, and optional loaded `Chart`; the load baseline guarantees explicit
  regions, so the region set is the complete tone set and the audible default is the engine's
  first-branch fallback (empty `audible_tone_ref`). The editor's rig composition to mirror
  byte-for-byte (constraint (g)) is rock-hero-editor/core/src/project/project_handlers.cpp
  ~:1036-1055: dedupe `tone_track.regions[].tone_document_ref` in schedule order, skip empties,
  `audible_tone_ref = {}`. Phase 2's "plus the default tone_document_ref" wording is therefore
  stale — the deduped region set alone is correct.
- Package IO: `readRockSongPackage(package_path, workspace_directory)` extracts a `.rock` into
  an existing workspace directory and parses it; a directory-based reader and writers also
  exist (rock-hero-common/core/include/rock_hero/common/core/package/rock_song_package.h).
  The reader hard-rejects `formatVersion != 1` (rock-hero-common/core/src/package/
  rock_song_package_read.cpp, ~line 976) — game-side version tolerance is
  docs/plans/roadmap/10-format-versioning-and-chart-identity.md scope.
- `rock-hero-game` is NO LONGER a skeleton (correction 2026-07-11; the original bullet predated
  plan 20 Phases 1–4 and plan 25 Phases 3–4): app/main.cpp is a plain portable `main()` under
  loop model L2 — SDL owns the frame loop, there is **no JUCEApplication**; the shell
  (`rock-hero-game/ui/src/surface/game_shell.cpp`) initializes JUCE message-pump-only via
  `juce::ScopedJuceInitialiser_GUI` (game_shell.cpp:159) and drains the JUCE queue each frame.
  game/core carries real tested code (frame clock, diagnostics, resources); game/ui hosts the
  shell + dev session; the highway renderer lives in rock-hero-common/ui. Game tests exist
  (rock-hero-game/core/tests). Phase 6's "existing JUCE game shell" wording is therefore stale —
  the engine embeds into the SDL3 shell whose message-pump coexistence plan 20's gate proved
  (S1); reconcile Phase 6's text when that phase executes.
- Adapter test infrastructure exists at rock-hero-common/audio/tests/test_engine.cpp (1,175
  lines) with a `drum_loop.wav` fixture wired through CMake, plus port contract tests at
  tests/test_transport.cpp (165 lines) with a `FakeTransport`. SIX transport test doubles exist
  repo-wide (correction: plan 47's "four FakeTransport implementations" predates the tone work,
  which added two anonymous-namespace `StubTransport`s): FakeTransport in test_transport.cpp,
  editor_controller_test_harness.h, editor_view_test_harness.h, and test_editor.cpp, plus
  StubTransport in test_tone_automation_lanes_view.cpp and test_tone_track_view.cpp — all must
  gain any new pure-virtual transport methods in the same commit (plan 47 Phase 1's coordination
  detail, absorbed here under the whichever-executes-first rule). Name-based searches miss the
  stubs; find implementers with `rg "public.*ITransport"`.
- docs/plans/roadmap/47-editor-loop-selection.md has NOT executed (status: Not started) — so under the
  whichever-executes-first rule Phase 1 here lands the FULL shared loop surface plan 47 Phase 1
  specified (setLoopRegion/clearLoopRegion/loopRegion, `LoopRegionTooShort`, 0.1 s minimum,
  engage sequence, arrangement-load clearing via a shared helper, four fake updates) in addition
  to the speed surface; plan 47 later re-verifies and only extends.
- docs/plans/todo/audio-engine-multi-track-support.md predates the current engine surface (it cites
  `Engine::createTrack()`/`IEdit`/`EditCoordinator`, none of which exist today) — noted for the
  roadmap disposition table, not absorbed here.

Verified against code on 2026-07-06, refactor @ 3c7febe0; re-verified for Phase 1 on 2026-07-11,
master @ 7ba93b90 (corrections above: engine port list gained IPlaybackClock/IToneAutomation and
the clock-boundary publishing pattern; game-skeleton bullet replaced with the SDL3-shell reality;
test inventory expanded; plan 47 non-execution recorded). engine_song_audio.cpp line references
drifted slightly (start-offset placement now ~:127, proxy-off comment ~:143-156, setGainDB ~:166,
`transport.looping = false` ~:170) — claims themselves re-verified intact; the backing clip also
gained `setSyncType(syncAbsolute)` from the tone work.

Re-verified for Phase 3 on 2026-07-11, same baseline: the multi-tone rack backend claims hold
exactly (buildToneRack/ToneRackBranch with per-branch `ToneBranchGainPlugin*` exposing an
automatable `branchGainParameter()`; `Engine::Impl::m_tone_rack` optional storage;
`setAudibleBranch` selection switching documented as replaced by baked automation during
playback). The in-progress plan's 2026-07-05 verified mechanism notes re-checked against the
vendored engine for the APIs Phase 3 uses: `AutomationCurve::addPoint(TimePosition, float,
float, UndoManager*)` (:127), `clear(UndoManager*)` (:86), `getValueAt(TimePosition, float)`
(:122), `Edit::getAutomationRecordManager().setReadingAutomation(bool)`
(tracktion_Edit.h:290, tracktion_AutomationRecordManager.h:43-44) — all present. **3d finding:**
the rig load ALREADY refuses on a missing VST (`ExternalPlugin::getLoadError()` non-empty →
`abortLiveRigLoad(PluginRestoreFailed, ...)` at engine_live_rig.cpp ~:1278-1285), aborting at
the FIRST failure — so answer 21-Q1(A)'s semantic exists and 3d reduces to aggregating ALL
missing plugins before refusing (a strict diagnostic improvement the editor path shares) plus a
distinct `MissingPlugins` error code the session maps for UI.

Re-verified for Phase 2 on 2026-07-11, same baseline; additional corrections: (1) arrangement
default-tone field deletion (bullet above); (2) rock-hero-game/core/tests already exists (plan 20
Phase 4 created it — test_diagnostics/test_frame_clock/test_game_resources) so Phase 2 EXTENDS
that target rather than creating one; (3) plan 12's IPlaybackClock is landed, so the session
exposes the clock port directly — Phase 2's "thin position() accessor until then" contingency is
dead; (4) editor slice 5c has NOT landed `IToneTimelinePlayer` (re-verified: no such symbol in
tree), so per Phase 3a/3b's own whichever-first wording, Phase 2 pulls forward the MINIMAL 3a
conversion (pure regions-to-seconds in common/core tone/, landed there directly — never
game-side) and the 3b port declaration (adopted shape from
docs/plans/in-progress/tone-track-tempo-map-plan.md "Runtime Audio Direction", with the schedule value
type in common/core because core cannot depend on audio); 3c gameplay-fit verification and 3d
missing-plugin fallback remain Phase 3's substance.

## Dependencies

- docs/plans/roadmap/20-game-architecture-and-render-stack.md — Phase 0b (JUCE MessageManager/Tracktion
  coexistence with the chosen game main loop). Gates only this plan's **final integration**
  shape; Phases 1-6 here run inside the existing JUCE game shell, which already owns a message
  loop.
- docs/plans/roadmap/12-playback-clock.md — its IPlaybackClock introduction phase. Phase 2 designs the
  GameplaySession to hand IPlaybackClock to gameplay consumers; until plan 12 lands, the session
  exposes message-thread `ITransport::position()` behind the same accessor seam.
- docs/plans/roadmap/13-audio-device-settings-and-calibration.md — shared per-device-identity settings
  store; Phase 6 uses fixed devices until it lands.
- docs/plans/roadmap/22-note-detection.md — Phase 1 detection contract defines the dry-signal tap point;
  this plan's Phase 5 records where the dry tap must sit (before the tone rack) so the two plans
  agree on one seam.
- docs/plans/roadmap/26-game-startup-menus-library.md — supplies real song selection; Phase 6 here
  hardcodes one package path.
- docs/plans/roadmap/27-in-song-flow-results-profiles.md — IGameSettings store persists Phase 4's mix
  values; until then they are session-local.
- docs/plans/roadmap/28-practice-mode.md — consumer of the speed factor and loop-region plumbing from
  Phase 1; its time-stretch quality verification does not block this plan.
- docs/plans/roadmap/47-editor-loop-selection.md — declares the same shared-port loop surface under a
  whichever-executes-first rule (its Decisions) and is expected to execute first, landing the
  Tracktion-backed loop; Phase 1 here then re-verifies the landed surface and adds only the
  speed methods.
- docs/plans/in-progress/tone-track-tempo-map-plan.md — slice 5 (runtime switching) is active editor
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
  (docs/plans/in-progress/tone-track-tempo-map-plan.md, Decisions 5-7; docs/design/architecture.md,
  "Timing and Latency"). Automation evaluates once per audio block, so switch onsets quantize to
  the block (~3-10 ms) with per-sample smoothing keeping transitions click-free — that meets the
  product bar; do not chase sample accuracy (same doc, Decision 7).
- **Seek resync is automatic**: parameter streams follow the transport position while stopped or
  scrubbing, so branch gains snap to the playhead without an explicit position push
  (docs/plans/in-progress/tone-track-tempo-map-plan.md, verified mechanism notes, citing
  plugins/tracktion_Plugin.cpp:676 in the vendored engine).
- **Latency compensation should be OFF for the live path**: `Edit::setLatencyCompensationEnabled
  (false)` is a public API (vendored tracktion_Edit.h:539); with PDC on, rack monitoring latency
  equals the worst branch at all times; with it off, latency equals the active branch only
  (docs/plans/in-progress/tone-track-tempo-map-plan.md, latency amendment 2026-07-05).
- **Gap behavior**: a tone-track gap holds the previous region's tone; before the first region
  the arrangement default tone (`tone_document_ref`) is audible
  (rock-hero-common/core/include/rock_hero/common/core/tone/tone_track.h ToneTrack docblock;
  docs/plans/in-progress/tone-track-tempo-map-plan.md, Validation).
- **FLAC is enforced by the package reader**; audio entering in other formats was transcoded on
  import (docs/design/architecture.md, "Technology Stack").
- **The game never mutates user packages** (docs/design/architecture.md, "Application
  Responsibilities": the game "Treats the `Song` model as read-only during gameplay").
- **Pre-activated silent plugins are the accepted CPU tradeoff**; do not design tone-count caps
  now (docs/plans/in-progress/tone-track-tempo-map-plan.md, preloading tradeoff note; matches
  docs/design/architecture.md "VST Plugin Safety" pre-activation guidance).

## Open questions for the user

All three ANSWERED 2026-07-11; Q1 and Q2 override the original recommendations (user decision —
fidelity-first, no soft-degrade paths):

1. **Missing-plugin fallback policy. ANSWERED: (A) — refuse to start the song, listing the
   missing plugins.** (Recommendation was B+C; the user chose strict fidelity: a song whose tone
   references uninstalled VSTs does not play until the player installs them.) Consequences for
   Phase 3d: rig-load failure due to missing plugins surfaces as a typed session Failed carrying
   the per-tone missing-plugin list for UI display; no partial-tone playback, no bundled
   substitute tone, and the "tone degraded" score-record marking is unnecessary (degraded runs
   cannot exist). **Pinned future enhancement (user, 2026-07-11): the refusal should eventually
   offer an opt-in "play with default tones" option — blocked on a default-tone mechanism that
   does not exist yet (a bundled clean tone asset; dovetails with plan 26's Q5 starter-song
   asset). Recorded as a watch item; the refusal UI should be shaped so the option can slot in
   without rework.**
2. **Per-tone reported-latency policy. ANSWERED (refined by the user, 2026-07-11): silent in the
   GAME — tones baked into songs are assumed good — but the guard moves to AUTHORING time: the
   editor warns on export/publish to `.rock` when any tone's summed reported latency is high, so
   no song can ship with an unintentionally high-latency tone.** Consequences: Phase 5 KEEPS the
   per-tone summed-latency surfacing through the rig-load result (its consumer is the editor's
   export warning, not a game UI); the export-time warning itself is editor work recorded in
   docs/tracking/backlog.md. A save-file flag marking high-latency tones (so players could be
   alerted) is explicitly DEFERRED — noted in the same backlog entry so it is not lost; it is a
   format change routed through plan 10 if adopted.
3. **Mix-volume scope. ANSWERED: global in v1** (as recommended) — persisted via
   docs/plans/roadmap/27-in-song-flow-results-profiles.md's IGameSettings once it lands;
   session-local until then; per-song override deferred until requested.

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
so docs/plans/roadmap/28-practice-mode.md is additive, not a rewrite:

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
  resync fact in Decisions above). Coordination: docs/plans/roadmap/47-editor-loop-selection.md Phase 1
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
  0 dB in v1; docs/plans/roadmap/26-game-startup-menus-library.md's cache may add game-side analysis
  later.
- `PreparingRig`: preload **every** tone referenced by the arrangement (deduped
  `tone_track.regions[].tone_document_ref` plus the default `tone_document_ref`) through
  `ILiveRig::loadLiveRig`, then hand the region schedule to the tone-timeline seam (Phase 3).
  The session must not report `Ready` until the completion callback fires — this is the
  pre-song preload guarantee: no plugin instantiation after `Ready`.
- `Ready/Playing`: `play()`, `pause()`, `seek`, instant restart (= `seek(start)` + `play()`,
  no rig teardown), and pass-throughs for speed/loop (Phase 1 surface) so plan 28 later drives
  them. Exposes the playback clock: IPlaybackClock once
  docs/plans/roadmap/12-playback-clock.md lands; a thin message-thread `ITransport::position()`
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
  (docs/plans/in-progress/tone-track-tempo-map-plan.md, sub-phase 5c). Coordination rule: if 5c has
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
  review in docs/plans/in-progress/tone-track-tempo-map-plan.md — same switching model, different
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
docs/plans/roadmap/27-in-song-flow-results-profiles.md's IGameSettings (record the key names there).
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
- Record the dry-tap contract: detection (docs/plans/roadmap/22-note-detection.md) taps the raw input
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
keyboard start — the audio half of the roadmap's milestone 0 (docs/plans/roadmap/00-roadmap.md). No
rendering dependency: this runs before plan 20's gate closes because the shell already owns a
JUCE message loop. Manual soak checklist: backing plays with normalization gain and
start-offset alignment; live guitar audible through the authored tone; tones switch at region
boundaries during play; instant restart and mid-song seek land on the correct tone; pause/
resume clean; mix controls act independently; a package with a missing plugin follows the
agreed fallback. Local-only corpus spot-checks (a handful of the 39 converted packages) —
never in CI, never committed.

Files: rock-hero-game/app/main.cpp plus a thin app-side composition unit; no new public
headers. **Composition placement (watch-item decision, user 2026-07-11): inject from `app/`** —
main.cpp composes the engine/session and hands GameShell injected ports; the shell keeps only
the frame loop and input wiring. This phase executes that restructuring alongside the session
wiring (and reconciles this phase's stale "JUCE game shell" wording against the SDL3 shell —
see the inventory correction). Testing plan: this phase is deliberately manual/soak; automated
coverage lives in Phases 1-5. Exit criteria: full playthrough of one real song with zero
dropouts and correct tone changes, witnessed and noted in the roadmap status.
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
(docs/plans/roadmap/00-roadmap.md) status line for this plan updated.

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
