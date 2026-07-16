# 22 — Note Detection

**Status**: Decision-gated (Gate A: detectability matrix + latency budget sign-off inside Phase 1;
Gate B: algorithm/dependency selection sign-off ending Phase 3). Date: 2026-07-06. Baseline
`refactor @ 3c7febe0`. **Phase 1 executed 2026-07-16** jointly with plan 24 Phase 1: detection
event types + tests landed in game/core (the "first game test target" framing below is obsolete —
`rock_hero_game_core_tests` already existed; the detection folder joined it), and the contract was
adversarially vetted by the dsp-guitar-detection-expert agent with its adjustments folded into the
budget, matrix, and metrics below, then amended the same day after a three-pass max-effort review
(see "Phase 1 contract vetting record" for both rounds). **GATE-A CLOSED 2026-07-16: the user
co-signed the vetted matrix and latency budget jointly with plan 24 — Phases 2+ are unblocked
(Gate B still stands ahead of Phase 6).**

## Goal

The player plugs in a real guitar and the game hears them: onsets and pitches are detected from
the dry input signal with known, measured latency and accuracy, published as typed events that
scoring (plan 24) consumes without ever touching audio. The first shippable consumer is a tuner —
cents-accurate against the arrangement's tuning including capo and cent offset — plus the
pre-song tuning gate, so detection is never blamed for a detuned guitar.

## Non-goals

- Scoring rules, hit windows, multipliers, star power, failure — docs/plans/roadmap/24-scoring-star-power-failure.md
  owns those; this plan only supplies the events, the detectability matrix draft, and the latency
  budget that 24's provisional-hit design must honor.
- The annotated fixture corpus, metric measurement harness, and replay tooling —
  docs/plans/roadmap/23-detection-verification-harness.md. This plan *defines* the accuracy metrics; 23
  *measures* them.
- String/fret disambiguation. Pitch detection yields sounding pitch, not string identity; the same
  pitch on two strings differs only in timbre. No phase here attempts it.
- Neural-network model training. A pretrained-model option may be surveyed in Phase 3, but no
  training pipeline is in scope.
- MIDI foot-controller input (plan 24 builds IMidiTrigger), practice-mode time stretch (plan 28),
  and any renderer work (plans 20/25).

## Constraints

Applicable subset of the roadmap constraint block (docs/plans/roadmap/00-roadmap.md):

- (a) **Layering**: common never depends on editor or game code; game never includes editor
  headers. The dry-signal tap must live in rock-hero-common/audio (the audio callback lives
  there); analysis and detection live in rock-hero-game/audio; event types and tuner/gate policy
  live in rock-hero-game/core. Tracktion headers stay isolated to rock-hero-common/audio
  implementation files — the tap adapter follows the existing src/tracktion/ pattern.
- (b) **Public-header minimalism**: one new public port per boundary (tap port in common/audio,
  detection-source port in game/audio); DSP internals stay private.
- (c) **Naming firewall**: never name the commercial real-guitar game; use "RS" or neutral
  phrasing in all files this plan produces.
- (g) **Tone fidelity** (tangent): the tap must not perturb the authored tone path — it is a
  read-only branch before the plugin chain; any tap mechanism that alters plugin-chain latency or
  gain staging is rejected.
- (h) **Builds**: all verification through `.agents/rockhero-build.ps1` (usage in
  `.agents/README.md`); intermediate phases run only determinately warranted checks; the final
  acceptance phase is the sanctioned bundle as separate invocations.
- (i) **Real guitar input**: analog audio is the only gameplay input signal; no plastic-controller
  assumptions anywhere in the detection contract.

Threading rules restated from docs/design/architecture.md "Threading Model" / "Rules": no locks,
heap allocation, or file IO on the audio thread; cross-thread communication via atomics or
lock-free queues; graph-rebuilding mutations are message-thread only.

## Current state inventory

- **No detection code exists.** rock-hero-game/audio and rock-hero-game/core are placeholder
  static libs (`rock-hero-game/audio/src/placeholder.cpp`, empty include tree
  `rock-hero-game/audio/include/rock_hero/game/audio/`). CMake targets exist:
  `rock_hero_game_audio`, `rock_hero_game_ui`, `rock_hero_game_exe`
  (rock-hero-game/audio/CMakeLists.txt:1, rock-hero-game/app/README.md). No game test targets
  exist yet.
- **No lock-free FIFO exists** anywhere in rock-hero-common/audio. The only audio-thread-to-UI
  channel today is Tracktion's `LevelMeasurer` client, read on demand via
  `rock-hero-common/audio/src/shared/meter_reader.h` (peak dB only — levels, not samples).
- **Live input port**: `rock-hero-common/audio/include/rock_hero/common/audio/input/i_live_input.h`
  — input gain (applied *before* the signal chain), raw input peak meter, processed-monitoring and
  calibration-monitoring toggles. All message-thread. No sample access.
- **Input calibration is gain calibration only**:
  `rock-hero-common/audio/include/rock_hero/common/audio/input/input_calibration.h` targets
  -6 dBFS peak / -12 dBFS RMS via a deterministic capture state machine. Latency calibration does
  not exist (plan 13 owns it).
- **The instrument route is mono**: `createTracktionInstrumentWaveDeviceDescriptions` builds a
  compact mono input + stereo output for the instrument track
  (rock-hero-common/audio/src/tracktion/tracktion_instrument_wave_device_mapping.h:66-78).
- **Project-owned Tracktion plugins are an established pattern**:
  `rock-hero-common/audio/src/tracktion/live_rig_gain_plugin.cpp` and
  `tone_branch_gain_plugin.cpp` — precedent for inserting a lightweight project plugin into the
  live chain.
- **Chart technique surface** (rock-hero-common/core/include/rock_hero/common/core/chart/chart.h):
  `NoteAttack{Pick,Hammer,Pull,Tap,Pop,Slap}`, `NoteMute{None,Palm,Full}`,
  `NoteHarmonic{None,Natural,Pinch}` plus fractional `touch`, `vibrato`, `tremolo`, `accent`,
  bend curves (`BendPoint{offset,semitones}`), slide waypoints (`SlideWaypoint{offset,fret,
  unpitched}`), sustain, `ChordTemplate`/`ChartShape`, `FretHandPosition`, `ChartSection`,
  `ChartTuning{strings[] as note-name-with-octave, capo, cent_offset}`.
- **Tuning bounds**: `g_max_chart_strings = 8`, `g_max_fret = 30`
  (rock-hero-common/core/include/rock_hero/common/core/chart/chart_rules.h:24,33). `cent_offset`
  is validated within a full octave because real bass arrangements charted on guitar strings pitch
  down -1200 cents (rock-hero-common/core/src/chart/chart_rules.cpp:15-16) — the latency budget
  must cover octave-down sounding pitches.
- **No note-name-to-frequency parser exists.** The only name/MIDI mapping is editor-private
  (`rock-hero-editor/core/src/project/gp_chart_builder.cpp:87-96`, MIDI-to-name direction only).
  The game cannot reach it (layering), so tuning pitch math is a common/core addition.
- **Design-doc mandate** (docs/design/architecture.md): "Threading Model" names an analysis thread
  in rock-hero-game reading a lock-free ring buffer of pre-effects guitar samples (2048-sample
  window / 512-sample hop example); "Timing and Latency" mandates all scoring comparisons in
  audio-sample time with calibration offsets, mixing thread clocks prohibited; "Gameplay Systems"
  names YIN/autocorrelation on the clean pre-effects signal combined with onset detection; the
  risk table rates "Pitch detection unreliable" High with mitigation "prototype early".
- **Agent**: `.claude/agents/dsp-guitar-detection-expert.md` is a roadmap-session deliverable
  created alongside this plan. If absent at execution time, create it first per
  docs/plans/roadmap/00-roadmap.md Deliverable 3 before running Phase 3.

Verified against code on 2026-07-06, refactor @ 3c7febe0.

## Dependencies

- docs/plans/roadmap/13-audio-device-settings-and-calibration.md — device identity, input-channel
  selection, and the measured latency-offset contract. Soft for the Phase 7 debug tuner (engine
  device-config port suffices); hard before the tuner/gate ships in onboarding.
- docs/plans/roadmap/12-playback-clock.md — correlating input-stream sample time to song time is the
  scoring consumer's job (plan 24) via IPlaybackClock; this plan only guarantees monotonic
  input-sample timestamps. No phase here blocks on 12.
- docs/plans/roadmap/21-game-audio-engine-and-session.md — the game embeds the common Engine, which is the
  live route the tap rides on. Phase 2 lands the tap in common/audio independently; 21 consumes it.
- docs/plans/roadmap/20-game-architecture-and-render-stack.md Phase 0 — final tuner presentation only. The
  Phase 7 tuner ships as a JUCE component in the current game shell and does not wait for the gate.
- Consumed by: docs/plans/roadmap/23-detection-verification-harness.md (all phases measure the metrics
  defined here); docs/plans/roadmap/24-scoring-star-power-failure.md (Phase 1 matrix + latency budget +
  event types); docs/plans/roadmap/26-game-startup-menus-library.md (onboarding tuner step);
  docs/plans/roadmap/29-online-leaderboards.md (its gate cites the stability criteria below);
  docs/plans/roadmap/45-editor-theme-and-string-colors.md (raising `g_max_chart_strings` past 8 adds
  sub-B0 fundamentals that stress this latency budget — coordinate before raising).

## Decisions already made

Restated from the cited sources; do not re-litigate:

- Detection taps the **clean pre-effects signal**; distortion flattens the spectrum and smears
  onsets — docs/design/architecture.md "Threading Model" (analysis-thread paragraph) and
  "Gameplay Systems".
- Real-time shape is **audio thread → lock-free ring buffer → analysis thread → lock-free results
  → game thread**, with the audio thread never locking, allocating, or blocking —
  docs/design/architecture.md "Threading Model", "Thread Communication".
- All scoring comparisons happen in **audio-sample time** with calibration offsets applied
  consistently; mixing audio-thread and render-thread time is prohibited —
  docs/design/architecture.md "Timing and Latency".
- Ports and adapters with project-owned interfaces at important boundaries; threading kept at the
  boundary so DSP cores are pure functions testable without threads; time is a dependency —
  docs/design/architectural-principles.md "Ports and Adapters", "Keep Threading at the Boundary",
  "Time Must Be a Dependency".
- The chart grid's ±0.5 ms quantization is below the onset-detection floor — precision above that
  is intentionally not chased (docs/design/architecture.md, timing precision note).
- Test targets link the production library they test, and pure unit tests are the preferred kind —
  docs/design/architectural-principles.md "Preferred Kinds of Tests".

## Open questions for the user

Mirrored into docs/plans/roadmap/00-roadmap.md Decisions-needed:

1. **v1 detectability tiers** (Phase 1 / Gate A): accept or adjust the draft matrix below —
   notably Pop/Slap = Cosmetic, pinch-harmonic timbre = Cosmetic, palm-mute quality = Cosmetic
   (note still scored), bend curve shape = Lenient with scored endpoint. Options per row: Scored /
   Lenient / Cosmetic. **Recommendation**: adopt the draft as v1 and revisit per-technique after
   plan 23 produces real per-class precision/recall numbers.
2. **DSP dependency policy** (Phase 3 / Gate B): (a) implement the chosen mono-pitch + onset
   algorithms in-repo (a few hundred lines each, fully testable, no license/Conan risk); (b) adopt
   a third-party DSP library (candidates surveyed in Phase 3; note aubio is GPL-3 and Essentia is
   AGPL — compatible with this repo's AGPLv3 but a real dependency-surface cost; Conan
   availability must be verified). **Recommendation**: (a) in-repo for v1; adopt a library later
   only if Phase 3 finds a decisive quality win the survey can cite.
3. **Tuner capo policy** (Phase 7): (a) gate on capo-on sounding pitches (open pitch + capo
   semitones + cent_offset) with a "place your capo at fret N" prompt — verifies the actual rig;
   (b) tune open strings without capo first, then prompt for capo placement unverified.
   **Recommendation**: (a) — it validates what the player will actually sound like in-song.
4. **Tuning-gate strictness default**: threshold that triggers the pre-song tuner interstitial.
   Options: per-string |error| > 10 cents; average > 5 cents; configurable with those defaults.
   **Recommendation**: trigger when any string is off by more than 10 cents (settled reading),
   always skippable, threshold user-configurable later via plan 27's settings.

## Phased implementation

### Phase 1 — Detection contract (no DSP)

**Scope**: Define everything downstream code depends on, before any signal processing exists.

1. **DetectionEvent types** as pure value headers in rock-hero-game/core, new feature folder
   `rock-hero-game/core/include/rock_hero/game/core/detection/`:
   - Common timestamp: `input_stream_sample` (uint64, monotonic position on the pipeline's
     *continuous virtual input stream*) + the stream sample rate. Song-time correlation is
     explicitly out of contract (plan 24 does it via plan 12's clock plus plan 13's offsets).
     A mid-session device restart (the Phase 2 tap's generation bump) rebases the virtual stream
     so positions stay monotonic; events never carry a generation, and whether a run survives a
     restart is the session's decision. Analysis-derived events (`PitchFrame`,
     `PitchConfirmation`, `PolyphonicSalience`) are anchored at the **last sample the analysis
     consumed** (the causal availability point) — never the window start or center — so latency
     metrics and scoring deadlines are producer-independent.
   - `OnsetEvent`: strength [0,1]; spectral character `Pitched | Percussive | Unknown` (the
     percussive class is how full mutes score without pitch); origin
     `Transient | PitchStep`. **Legato emission rule**: hammer-ons/pull-offs/taps often have no
     pick transient, so when the sustained-pitch tracker observes a discrete f0 step (≥ 1
     semitone within ≤ 2 hops, k consistent frames at the new pitch, no intermediate-glide
     occupancy — glides are bends/slides, not steps) it publishes an OnsetEvent with
     `origin = PitchStep`, `character = Pitched`, strength = step confidence, **back-dated to
     the first frame of the new pitch**. A PitchStep onset is suppressed within 30 ms after a
     Transient onset (the pick's own attack glide must not double-fire). **Strum-coalescing
     rule**: transients within a 30 ms combine window (the literature's annotation/peak-picking
     convention) belong to one gesture and publish one onset timestamped at the first transient
     — chosen because 32nd notes at 240 BPM (31.25 ms) bound the fastest legitimate playing
     from above; the tremolo re-onset threshold must stay ≥ 2× this window so the two constants
     cannot drift into conflict. Known, honest bias: first-transient anchoring reads wide slow
     strums up to ~10–20 ms early if the player centers them on the beat; visible in plan 27's
     tendency data and re-centerable by a ruleset constant.
   - `PitchFrame`: periodic (per hop) f0 estimate in Hz, confidence [0,1], clarity/periodicity,
     frame RMS — the tuner and sustain/bend tracking consume the stream form; hops are never
     skipped (zero-confidence frames still flow), which is what makes scoring's evidence-gated
     deadline decisions replay-deterministic.
   - `PitchConfirmation`: onset-associated confirmed pitch (f0, confidence, confirming-frame span)
     — what plan 24's provisional-hit state machine waits for.
   - `PolyphonicSalience`: the chart-blind chord-evidence carrier — one snapshot per onset with
     up to 8 detector-ranked (f0, salience) candidates (fixed array, unused slots zeroed).
     Scoring checks charted member pitches against the candidate set; detection never sees the
     chart, so replay logs stay re-scoreable against edited charts. Bound by the same
     per-register confirmation targets as `PitchConfirmation` (a 2048-sample salience window at
     48 kHz fits the E2 row; the 4096 window low registers need fits theirs).
   - All types are serializable by design intent (plain values, no handles, trivially copyable
     including the variant composite) so plan 23 can replay event logs deterministically; the
     serialization format itself lands in plan 23 and must serialize field-wise (struct tail
     padding is indeterminate).
2. **Technique detectability matrix** (draft below) — co-authored with plan 24 Phase 1; the
   normative scored/lenient semantics live in 24, the acoustic-feasibility judgment lives here.
3. **Dry-signal tap specification**: tap is mono, post-input-gain (so plan 13's gain calibration
   normalizes detection input level), pre-plugin-chain, read-only.
4. **Latency budget** (below) and **accuracy metric definitions** (below) adopted as the contract.

**Draft detectability matrix (v1)** — Scored = detected and affects score; Lenient = detected with
wide tolerance / partial verification; Cosmetic = rendered but not verified at v1:

| Chart field | v1 tier | Basis |
|---|---|---|
| Note onset/timing | Scored | Onset detection is the most reliable signal available (picked/plucked attacks; hammer/pull/tap notes take their row's lenient onset path) |
| Single-note pitch | Scored | Sounding pitch only; string/fret identity is not observable |
| Chords (multi-string) | Lenient (staged) | v1: onset + mono any-member rule — a confirmed f0 matching any chord member (octave-insensitively) confirms, a confident persistent non-member revokes, else deadline default. Promotion: when plan 23 measures member-verification P/R ≥ 0.90 per voicing class, the `PolyphonicSalience` evidence upgrades the rule to "≥ 2 distinct non-octave member pitch classes present, no confident non-member." Octave doublings are physics-unverifiable (every partial coincides), so octave-dyad chords always use the any-member rule; exact voicing stays unverified permanently |
| Sustain hold | Scored | Sustained-pitch tracking with decay-aware confidence leniency |
| Bend (`bend[]`) | Lenient | Endpoint pitch scored ± tolerance; curve shape cosmetic at v1 |
| Slide waypoints | Lenient | Glide direction + terminal pitch; `unpitched` slides cosmetic |
| Vibrato | Lenient | Periodic f0 modulation presence; needs ~2 modulation cycles at the 4–7 Hz vibrato band, so only verifiable on sustains ≥ ~300–500 ms depending on rate — shorter vibrato stays cosmetic; depth/rate unverified |
| Tremolo | Lenient | Re-onset rate above threshold; exact count unverified |
| Hammer/Pull/Tap attack | Lenient | Pitch change without strong pick transient; picked also accepted |
| Pop/Slap attack | Cosmetic | Spectral cues too rig-dependent at v1 |
| Palm mute | Cosmetic (mute quality) | Note itself Scored as a pitched onset |
| Full mute | Scored (onset timing); class check Lenient | Onset timing scores; the percussive-unpitched class check confirms early when present but stays lenient (best published prior ~83% on clean guitar) — promotion to a scored class check requires plan 23 measuring per-class P/R ≥ 0.90; pitched evidence never penalizes a mute-charted note |
| Natural harmonic | Lenient | Expected sounding pitch computed from `touch`/node; wide tolerance |
| Pinch harmonic | Cosmetic (timbre) | Overtone selection is chaotic across rigs; onset Scored |
| Accent | Cosmetic | Player/instrument absolute-level variability defeats a portable threshold (the pre-effects tap sees no compressor, so relative dynamics ARE observable — a measured upgrade path exists) |
| Shapes / fingerings / FHP | Cosmetic (permanently) | Not acoustically observable |

**Latency budget** — the hard floor is physics: an autocorrelation-family estimator fundamentally
needs ~**2 fundamental periods** of signal (MPM/NSDF is designed for two-period windows; YIN's
difference-function structure implies the same bound). The third period in the table is
engineering margin (confidence gating, normalization warm-up), and the pluck attack adds dead
time before the usable-period clock starts — attack transients are aperiodic and carry an initial
sharp pitch glide from tension modulation, so budget 5–8 ms of transient allowance. Sounding pitch
includes `cent_offset` (-1200 cents is a validated, corpus-real practice — chart_rules.cpp:15-16):

| Lowest sounding pitch | f0 | Period | 3 periods | Confirmation target (p95, sample time) |
|---|---|---|---|---|
| E2 (standard guitar) | 82.4 Hz | 12.1 ms | 36 ms | ≤ 50 ms |
| B1 (7-string / drop B) | 61.7 Hz | 16.2 ms | 49 ms | ≤ 65 ms |
| E1 (bass standard; guitar chart at -1200c) | 41.2 Hz | 24.3 ms | 73 ms | ≤ 90 ms |
| B0 (5-string bass; 7-string at -1200c) | 30.9 Hz | 32.4 ms | 97 ms | ≤ 115 ms |

Decomposition on top of the estimator window: attack-transient dead time (5–8 ms), hop quantum
(5.3 ms at a 256-sample hop **at 48 kHz** — every millisecond figure in this decomposition
assumes that rate; at 44.1 kHz the same hop is 5.8 ms), analysis compute (budget ≤ 2 ms per
hop), and multi-frame confirmation gating ((k−1) × hop when confirmation requires k agreeing
hops; k = 1 at v1 until measurement argues otherwise). **Pre-stated tripwire**: the E2 row has
zero slack for k ≥ 2 — if plan 23's octave-error measurements force k = 2, the E2 target is
revised to ≤ 55 ms as a recorded budget revision, never discovered as a silent CI failure. All
targets are defined in **input-stream sample time** — confirmation-event sample position minus
true onset sample position — so plan 23 measures them deterministically from replay logs.
Device I/O buffering (device buffer plus driver safety offsets) affects only the player's
wall-clock experience; it is plan 13's measured-calibration territory and is never assumed from
the buffer size. Onset events do not wait for pitch: **onset p95 ≤ 15 ms** after the physical
transient (sample time), which requires a strictly causal peak-picker — a single frame of
lookahead spends 5.3 ms of that budget. This 15 ms target applies to `Transient`-origin onsets
only: a `PitchStep` (legato) onset cannot exist before the tracker re-locks on the new pitch, so
its note-start latency is bounded by the new pitch's register row in the table above instead —
back-dated timestamps keep its hit-window comparison honest. The onset F1 target and the onset
latency target must be met simultaneously by one configuration, never traded against each other
across separate runs. These two numbers — fast onset, slow pitch — are exactly why plan 24's
provisional-hit state machine is mandatory, not optional: pitch confirmation on low strings
consumes most of a GH-style hit window.

**Accuracy metrics** (defined here; plan 23 measures every one of them in CI):

- Onset: precision / recall / F1 against annotations with ±25 ms matching tolerance and strict
  one-to-one counting (each annotation matched at most once; merged-onset counting inflates
  scores). ±25 ms is deliberate — the literature argues it over the older ±50 ms convention for
  online detection, and a rhythm game whose whole hit window is ~±100 ms cannot grade itself with
  a ±50 ms ruler; F1 at ±50 ms is dual-reported for literature comparability only. **Chart notes
  sharing a GridPosition collapse to one ground-truth onset** (the literature's
  annotations-combined-within-30 ms convention) — otherwise a correctly merged six-string strum
  scores five false negatives by definition. Legato note starts (hammer/pull/tap) count as onset
  annotations, and P/R plus latency are reported per origin class (`Transient` vs `PitchStep`) —
  the published soft-onset recall gap is precisely what the per-class split must expose.
  Detection latency distribution p50/p95 in sample time.
- Pitch: frame-level raw pitch accuracy (within ±50 cents of annotation, over voiced frames — the
  form sustain/bend tracking is graded on) and note-level confirmed-pitch accuracy (per onset —
  the form scoring is graded on), reported separately; gross-error rate; octave-error rate
  bucketed per register class — ≤ 2% is binding for E2+, and the E1/B0 buckets are provisional
  until plan 23's synthetic low-register sweeps set evidence-based bounds (octave errors
  concentrate exactly where fundamentals are weak); time-to-confirmation p50/p95 bucketed per
  register class (E2+, B1, E1, B0). The octave-error metric is also the arbiter of the k = 1
  confirmation policy: single-hop confirmation forgoes the temporal smoothing that reduces
  octave errors, so if ≤ 2% fails at k = 1, the k = 2 budget tripwire above fires — that loop is
  closed by design, not accident.
- Chord evidence: member-verification precision / recall per voicing class (power chords, octave
  dyads, 4–6-string voicings reported separately) — the promotion trigger (≥ 0.90) for the
  staged chord rule; plus `PolyphonicSalience` event latency per register.
- Sustain: fraction of annotated sustain span with in-tolerance tracked f0.
- Technique classifiers: per-class precision / recall (percussive-mute class, harmonic presence,
  vibrato presence, tremolo re-onset detection). The percussive-mute numbers carry the full-mute
  matrix row's promotion trigger (≥ 0.90).
- Tuner: cents bias and standard deviation on settled steady tones (|bias| ≤ 1 cent, σ ≤ 2 cents),
  update rate ≥ 10 Hz, settle time ≤ 300 ms after pluck. Bias is referenced to the fundamental
  partial of the post-glide steady state — plucked attacks run systematically sharp while the
  tension-modulation glide decays, so readings taken during the attack are excluded from the bias
  metric by definition, and inharmonicity-stretched upper partials make "the pitch" ambiguous
  unless the fundamental is named as the reference.
- v1 targets: onset F1 ≥ 0.95 and pitch RPA ≥ 0.95 on clean DI single notes; octave errors ≤ 2%
  (E2+); latency within the budget table, met simultaneously with the accuracy targets by one
  configuration. Plan 29's leaderboard gate additionally requires these numbers stable (no
  regression > 1 point) across three consecutive plan-23 corpus runs.

**Phase 1 contract vetting record (2026-07-16)** — the dsp-guitar-detection-expert agent
adversarially reviewed the draft budget, matrix, and metrics against primary literature before
Gate A. Five adjustments were folded into the text above: (1) the 2-period estimator floor is the
physics, the third period is margin, and attack-transient dead time (5–8 ms) plus k-frame
confirmation gating were added to the decomposition — E2/E1/B0 confirmation targets moved to
50/90/115 ms because the draft's own components summed past its E2 target; (2) all latency
targets are sample-time (replay-measurable), with device I/O split out to plan 13's wall-clock
calibration; (3) the full-mute class check demoted to Lenient with a ≥ 0.90 P/R promotion trigger
(best published prior for expression-class detection on clean guitar is ~83%, versus ~98% for
onsets); (4) the accent rationale corrected — the pre-effects tap sees no compressor, the real
constraint is player/instrument level variability; (5) metric addenda — strict onset counting
named, ±50 ms dual-reporting, frame-level vs note-level pitch accuracy split, per-register
octave-error buckets, tuner bias referenced to the post-glide fundamental. Everything else was
confirmed as drafted, including ±25 ms matching and the 15 ms onset p95 (as sample-time with a
strictly causal peak-picker). Key sources: McLeod & Wyvill 2005 (MPM/NSDF two-period windows);
Böck, Krebs & Schedl, ISMIR 2012 (online onset detection, ±25 ms evaluation); Kehling et al.,
DAFx-14 + IDMT-SMT-Guitar (~98% onset/multipitch, ~83–93% expression classes on clean electric
guitar); Bello et al. 2005 (onset-detection tutorial; soft-onset weakness); de Cheveigné &
Kawahara 2002 (YIN) and Mauch & Dixon 2014 (pYIN) for octave-error rates. Open verification
items carried into Phase 3 / plan 23: exact YIN gross-error tables (primary PDF unreachable
during vetting), full-mute per-class P/R on our corpus (the promotion trigger), and sub-E2
octave-error bounds from the synthetic sweeps.

**Max-effort review amendments (2026-07-16, same day)** — before Gate A signature, three further
independent adversarial passes ran (a second DSP literature pass with primary sources fetched, a
full code review of the frozen headers, and a game-feel walkthrough of concrete scenarios), and
their converged findings were folded into the contract: (1) `OnsetOrigin` (`Transient` /
`PitchStep`) added with the legato emission rule, back-dating, per-register legato latency
bounds, and per-origin metrics — a transient-only vocabulary could not hear hammer-ons at all
(Collins ISMIR 2005: pitch-track segmentation beats energy detectors on soft onsets);
(2) `PolyphonicSalience` frozen as the chart-blind chord-evidence carrier with the staged
any-member → ≥ 2-distinct-members chord rule (real-time multi-f0 salience verified feasible:
Dessein/Cont/Lemaitre ISMIR 2010 ran fixed-basis NMF 3× real time in MATLAB on 2010 hardware;
octave members are physics-unverifiable — 2:1 partial coincidence is total); (3) the 30 ms
strum-coalescing window with first-transient timestamps and the chord ground-truth collapse
metric rule (Böck & Widmer DAFx-13 and madmom both combine onsets within 30 ms; 32nd notes at
240 BPM bound legitimate playing at 31.25 ms); (4) analysis-event timestamps pinned to the
last-consumed-sample anchor and stream positions defined on a continuous virtual stream across
device restarts — both were producer-dependent ambiguities worth up to a full analysis window;
(5) the 48 kHz decomposition assumption named, the vibrato bound corrected to ~300–500 ms
(2 cycles at 4–7 Hz), and the k = 2 → E2 ≤ 55 ms tripwire pre-stated with the octave-error
metric as its arbiter. Scoring-side counterparts (evidence-gated confirm-by-default, overstrum
policy, revoke-to-Armed, committed-only display counters, sustain trajectory tolerance) are
recorded in plan 24 §6/§9. New open items for plan 23: per-voicing chord member P/R (the
promotion trigger), legato onset recall/latency on DI material, the strum-spread distribution of
real players (does 30 ms cover p95?), and salience-event latency per register.

**Files touched**: new headers under rock-hero-game/core include tree; new test target
`rock_hero_game_core_tests` (first game test target; links `rock_hero::game::core` per the
test-target convention). CMake graph changes.

**Public-header impact**: new public value-type headers in game/core (game-internal surface).

**Testing**: value-semantics and ordering tests for the event types in
`rock-hero-game/core/tests/test_detection_event.cpp` (events order by `input_stream_sample`;
comparison/equality behave as values).

**Exit criteria**: headers compile and tests pass; matrix + latency budget + metric definitions
presented. **STOP — Gate A: present the matrix and budget for user sign-off (open questions 1)
jointly with plan 24 Phase 1 before either plan proceeds.**

**Verification**:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\.agents\rockhero-build.ps1 -Configure -Targets all
powershell -NoProfile -ExecutionPolicy Bypass -File .\.agents\rockhero-build.ps1 -RunTouchedTests
```

### Phase 2 — Dry-signal tap port in common/audio

**Scope**: The audio callback lives in rock-hero-common/audio, so per constraint (a) the tap is
extracted to common FIRST, as its own phase with tests, before game code consumes it.

- New port `ILiveInputTap` in
  `rock-hero-common/audio/include/rock_hero/common/audio/input/i_live_input_tap.h`: a consumer
  registers a realtime sink (message-thread registration, audio-thread delivery); frames arrive as
  mono float blocks tagged with the monotonic input-stream sample position and sample rate. The
  delivery mechanism is a bounded lock-free SPSC FIFO owned by the sink; overflow drops oldest and
  increments a visible overrun counter (never blocks the audio thread).
- Tracktion adapter in `rock-hero-common/audio/src/tracktion/`: preferred mechanism is a
  lightweight pass-through tap plugin at the head of the instrument chain, following the
  established `live_rig_gain_plugin.cpp` pattern — post-input-gain, pre-tone-rack, bit-transparent
  to the tone path (constraint (g)). **Checkpoint: verify with juce-tracktion-expert before
  implementing** — candidate insertion points (project tap plugin vs `InputDevice`-level consumer
  callback) and their behavior across graph rebuilds, device restarts, and monitoring-mode
  transitions; cite file:line findings from external/tracktion_engine in the implementation
  commit.
- Sample-position continuity across device restarts: position restarts at zero with a generation
  counter bump; consumers treat a generation change as a stream reset.

**Files touched**: new public header + FIFO/value headers in common/audio; new adapter TU(s) in
src/tracktion/; `engine_live_input.cpp` wiring; tests in rock-hero-common/audio/tests.

**Public-header impact**: `i_live_input_tap.h` plus a small frame-block value header. FIFO
implementation stays private if possible; it becomes public only if the sink type requires it.

**Testing** (`test_live_input_tap.cpp` in rock-hero-common/audio/tests, added to
`rock_hero_common_audio_tests`): SPSC FIFO correctness under interleaved bounded write/read;
overrun counting; generation-reset semantics; adapter test that a registered sink receives frames
with strictly increasing sample positions from a fake feed.

**Exit criteria**: port merged, tests green, tone path verified undisturbed (existing
monitoring-mode transition tests still pass).

**Verification**:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\.agents\rockhero-build.ps1 -Configure -Targets rock_hero_common_audio_tests
powershell -NoProfile -ExecutionPolicy Bypass -File .\.agents\rockhero-build.ps1 -RunTouchedTests
```

### Phase 3 — Algorithm survey and selection (research)

**Scope**: No production code. The executor MUST run this phase through the
`dsp-guitar-detection-expert` agent (`.claude/agents/dsp-guitar-detection-expert.md`) plus its own
online research, and document the survey verbosely with source citations. Cover at minimum:

- Mono pitch: YIN, MPM/McLeod, pYIN (probabilistic HMM smoothing), SWIPE'; CPU-cheap neural
  options (CREPE-class) noted with cost; latency-vs-accuracy behavior at 30–90 Hz fundamentals.
- Onset: spectral flux with adaptive thresholding, SuperFlux, complex-domain, and time-domain
  transient detection; behavior on palm-muted chugs vs picked notes vs hammer-ons.
- Sustained-pitch tracking for bends/vibrato/sustains (median-filtered f0 trails, HMM tracking).
- Percussive/harmonic discrimination (spectral flatness, periodicity confidence, HPSS) for the
  full-mute class.
- Polyphonic reality check: multi-f0 estimation limits for chord salience (Lenient tier only).
- Library scan with licenses and Conan availability (aubio GPL-3, Essentia AGPL, cycfi/Q MIT,
  others found in research) versus in-repo implementation cost per algorithm.

**Deliverable**: a selection memo appended to this plan (this plan is being executed, so keeping
it aligned is required by CLAUDE.md's docs/plans/roadmap maintenance rule once the roadmap lands it), with
per-algorithm citations and a recommendation resolving open question 2.

**Exit criteria**: **STOP — Gate B: present the selection memo and dependency recommendation for
user sign-off.** No verification commands (documentation-only phase).

### Phase 4 — Tuning pitch math in common/core (pure)

**Scope**: The math both the tuner and future chart validation (docs/plans/roadmap/42-chart-validation.md)
need, placed in common/core because game and editor both need it eventually (constraint (a):
extract to common first).

- New header `rock-hero-common/core/include/rock_hero/common/core/chart/tuning_pitch.h`:
  note-name-with-octave → MIDI number parsing (the format `ChartTuning::strings` already uses);
  MIDI + cents → frequency (A4 = 440 Hz); per-string expected sounding pitch for a `ChartTuning`
  (open pitch + capo semitones + `cent_offset`); cents difference between a measured f0 and a
  target.
- Optional cleanup (do not force): migrate the editor's private MIDI-to-name table
  (rock-hero-editor/core/src/project/gp_chart_builder.cpp:87-96) onto the shared mapping.

**Public-header impact**: one new common/core header of pure functions.

**Testing** (`test_tuning_pitch.cpp` in rock-hero-common/core/tests): standard/drop/bass tunings,
capo shifts, ±1200-cent offsets, malformed name rejection, round-trip with the editor's naming.

**Exit criteria**: tests green; frequencies for E2/B1/E1/B0 match the latency-budget table.

**Verification**:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\.agents\rockhero-build.ps1 -Targets rock_hero_common_core_tests
powershell -NoProfile -ExecutionPolicy Bypass -File .\.agents\rockhero-build.ps1 -RunTouchedTests
```

(Use `-Configure` first if the test source list changed the CMake graph.)

### Phase 5 — Analysis pipeline skeleton in game/audio

**Scope**: The real-time shell, with no real DSP yet (a pass-through "energy onset" stub is
enough to prove the plumbing).

- `rock-hero-game/audio`: analysis-thread owner consuming an `ILiveInputTap` sink FIFO; fixed
  internal processing rate with windowing/hop configuration; publishes DetectionEvents (Phase 1
  types) through a bounded lock-free queue behind a new port
  `rock-hero-game/audio/include/rock_hero/game/audio/detection/i_detection_source.h` (poll-based
  drain from the game thread — no callbacks into consumers from the analysis thread).
- Per docs/design/architectural-principles.md "Keep Threading at the Boundary": the pipeline core
  is a pure `processFrames(span<const float>) → vector<DetectionEvent>` object; the thread is a
  thin shell around it. Plan 23's harness and all tests drive the core directly, threadless.
- Timestamps: every event carries the input-stream sample position derived from the tap's frame
  tags, never wall-clock time (docs/design/architecture.md "Timing and Latency").

**Public-header impact**: `i_detection_source.h` plus a pipeline-config value header in game/audio.

**Testing** (new target `rock_hero_game_audio_tests`, links `rock_hero::game::audio`): deterministic
frame-feed tests (synthetic buffers in, events out, exact timestamps); thread-shell start/stop and
drain; FIFO overrun surfacing.

**Exit criteria**: stub events flow end-to-end from fed frames with correct sample timestamps.

**Verification**:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\.agents\rockhero-build.ps1 -Configure -Targets all
powershell -NoProfile -ExecutionPolicy Bypass -File .\.agents\rockhero-build.ps1 -RunTouchedTests
```

### Phase 6 — v1 detectors (assumes Gate B outcome)

**Scope**: Implement the Phase 3 selections inside the Phase 5 core (all private to game/audio):
mono pitch estimator, onset detector, sustained-pitch tracker, percussive-mute classifier v1.
Tuner-grade steady-state pitch quality is the first bar; onset latency the second.

**Testing**: in-repo synthetic fixtures only (sine, sawtooth, Karplus-Strong-style plucks with
decay across 30.9–330 Hz fundamentals; these are generated, license-free, CI-safe). Assert the
Phase 1 metrics on synthetic material: cents accuracy, octave-error absence, onset latency bounds.
The real measured numbers come from docs/plans/roadmap/23-detection-verification-harness.md — its corpus
phases are the acceptance authority for the v1 targets; converted-content corpora stay local-only
and never enter CI.

**Exit criteria**: synthetic-fixture metric tests green; plan 23 baseline run recorded.

**Verification**:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\.agents\rockhero-build.ps1 -Targets rock_hero_game_audio_tests
powershell -NoProfile -ExecutionPolicy Bypass -File .\.agents\rockhero-build.ps1 -RunTouchedTests
```

### Phase 7 — Tuner: first shippable consumer

**Scope**:

- game/core (pure): `TunerReading` (nearest target string from the arrangement's `ChartTuning` via
  Phase 4 math, cents offset, settled/held state) computed from the `PitchFrame` stream;
  per-string in-tune verdicts.
- game/ui: a JUCE tuner component hosted in the existing game shell
  (rock-hero-game/app/main.cpp's DocumentWindow) — deliberately does NOT wait for
  docs/plans/roadmap/20-game-architecture-and-render-stack.md Phase 0; presentation is revisited after
  that gate closes. Device/input selection uses the engine's existing device-config port until
  docs/plans/roadmap/13-audio-device-settings-and-calibration.md lands the shared store.
- Capo handling per open question 3's resolution (recommended: target capo-on sounding pitches
  with a placement prompt).

**Public-header impact**: tuner domain headers in game/core; one component header in game/ui.

**Testing**: pure game/core tests feeding scripted `PitchFrame` sequences and asserting readings,
settle behavior, and per-string verdicts (`rock_hero_game_core_tests`). UI stays humble — logic
lives in core per docs/design/architectural-principles.md "Humble Object, But With the Right
Scope".

**Exit criteria**: with a real guitar on a real interface, the tuner tracks all six standard-tuning
strings within the tuner metric targets; corpus tunings (drop, octave-down) selectable and correct.
This is also the milestone-0 de-risk checkpoint the roadmap sequences early.

**Verification**:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\.agents\rockhero-build.ps1 -Targets all
powershell -NoProfile -ExecutionPolicy Bypass -File .\.agents\rockhero-build.ps1 -RunTouchedTests
```

### Phase 8 — Pre-song tuning gate policy

**Scope**: Pure policy in game/core: given the arrangement's `ChartTuning` and recent settled
tuner readings, produce `Pass | PromptTuner(strings out of tolerance) | PlayerSkipped`. Threshold
per open question 4's resolution; always skippable (constraint: real guitars drift — blocking play
outright punishes the player). Screen wiring into the song-start flow belongs to
docs/plans/roadmap/26-game-startup-menus-library.md (onboarding) and
docs/plans/roadmap/27-in-song-flow-results-profiles.md (pre-song flow); they consume this policy object.

**Testing**: pure tests in `rock_hero_game_core_tests` — in-tune pass, single-string fail listing
the string, skip recording, threshold boundary cases.

**Exit criteria**: policy tests green; consuming plans unblocked.

**Verification**:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\.agents\rockhero-build.ps1 -Targets rock_hero_game_core_tests
powershell -NoProfile -ExecutionPolicy Bypass -File .\.agents\rockhero-build.ps1 -RunTouchedTests
```

## Final acceptance phase

Run the sanctioned bundle as separate invocations from the repo root, after the last code phase:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\.agents\rockhero-build.ps1 -Targets all
powershell -NoProfile -ExecutionPolicy Bypass -File .\.agents\rockhero-build.ps1 -RunTouchedTests
powershell -NoProfile -ExecutionPolicy Bypass -File .\.agents\rockhero-build.ps1 -Targets clang-tidy
pre-commit run --all-files
```

Acceptance additionally requires: the plan-23 baseline metrics report exists and meets the v1
targets on synthetic fixtures, and the tuner exit criterion (Phase 7) has been demonstrated on
real hardware.

## Rollback/abort notes

- **Phase 2 (tap adapter)**: if the tap plugin destabilizes the live chain across graph rebuilds
  or monitoring-mode transitions, fall back to the `InputDevice`-level consumer tap (pre-input-gain;
  compensate by applying the current `ILiveInput::inputGain()` value inside the analysis pipeline).
  The `ILiveInputTap` port shape is designed to survive that swap unchanged; abort criterion is
  any audible artifact or added latency in the tone path (constraint (g)).
- **Phase 6 (detectors)**: algorithms live behind the pipeline core's internal seams; a failed
  algorithm choice is replaced without touching the port, the event types, or any consumer. Keep
  the synthetic-fixture metric tests as the regression net across swaps.
- **Systemic risk** (docs/design/architecture.md risk table: pitch detection unreliable — High):
  if low-register pitch confirmation cannot meet the budget on real material, the sanctioned
  degradation is onset-weighted scoring — onsets remain Scored, low-register pitch drops to
  Lenient — decided with the user and recorded in plan 24's matrix revision, never silently.
- Phases 1, 4, 5, 7, 8 are additive (new files, new targets); rollback is deleting the additions —
  no existing behavior is modified except the Phase 2 engine wiring.
