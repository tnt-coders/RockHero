# Backlog

Small fixes and evaluations to do when there's time — short entries, not plans. Counterpart to
[watch-items.md](watch-items.md): backlog items you *do*, watch items you *monitor*. Anything
that needs a design or multiple steps belongs in a `docs/plans/todo/` plan instead. Delete an
entry when it's done — git history is the record.

## Reading the 2026-08-10 review sections below

Five parallel reviews ran that day (audio, the game side plus the shared highway, editor UI, a
whole-codebase consistency sweep, and a documentation-accuracy audit) and most of what they found was
fixed on the `code-review` branch the same day. What remains here is the residue, in two kinds, so
check which kind an entry is before picking it up:

1. **Ordinary backlog items** — verified, small, and just waiting for time.
2. **Items carrying a ruling** — the user decided something on 2026-08-10 and the entry records the
   decision and what it changed; read the ruling before the remedy.

(The review's design-level residue lives in `docs/plans/todo/`, per this file's charter — the
FOR FABLE items that briefly sat here were either executed the same day or moved to
`design-calls-from-the-2026-08-10-review.md`.)

Two findings became plans rather than entries, because they are too large for this file:
`docs/plans/roadmap/57-positions-past-the-drawn-board.md` (the board draws 24 frets while the chart
domain allows 30 and nodes 48) and the onset-grouping move, EXECUTED 2026-08-10 and now at
`docs/plans/completed/highway-onset-groups-into-the-projection.md`. Executing the second opened a
**product decision that is now genuinely decidable**: the highway hides note heads on a repeat
that the 2D lane shows in full, and with the classification in core either surface can read it —
the user picks which way the two surfaces reconcile.

## Found by the 2026-08-10 save/undo and timeline reviews

The severe half shipped the same day: the package write is atomic, a NaN automation value is refused,
`project.json` closes before its success check, the undo clean marker compares against the depth the
write captured, and openness no longer reads an optional a write deliberately empties. These are the
rest, each verified against the code, each a fix rather than a question unless marked.

- **A superseded busy token drops its `after_cleared` continuation**, so a caller whose continuation
  RESTORES state never gets it. Applying an audio device and then triggering Close or Exit leaves the
  settings dialog hidden and non-modal forever, live-input monitoring permanently disabled because its
  closed notification never fires, and the controller's destructor cancelling the device change the
  user successfully applied. Fix DELETES the asymmetry — run it on both supersede returns, matching
  the invariant the audio-device refusal path already upholds. `test_busy_operation_workflow.cpp`
  currently ASSERTS the drop, so the test encodes the defect and changes with it.
- **The song-document builder writes while it validates.** Its per-arrangement loop writes the chart
  file and copies audio in the same iteration that validates, so a failure on arrangement N leaves
  N-1 rewritten and its audio copied while `song.json` still describes the old state. Its own comment
  claims the opposite and is true only within one iteration.
- **Arrangements sharing one external backing file each get a copy, and the copies compound.** The
  dedup map is consulted after the import and keyed on the destination, so three arrangements
  pointing at one external file produce three identical copies — and the next save copies again
  under fresh unique names (`uniqueAudioPath` deliberately picks a name that does not exist, and
  nothing rewrites session paths after a save). Key the dedup map on the SOURCE path so the lookup
  precedes the copy.
- **Two authorities for which meter governs a moment.** The seconds-keyed lookup reads a column filled
  through a CLAMPING resolver, so every signature change past the terminal anchor collapses onto the
  last anchor's time and disagrees with the measure-keyed lookup. Bounding a signature's measure by
  the terminal anchor in validation is the fix; deriving one from the other is precision-fragile
  exactly at a meter-change downbeat, and that yield is the signal the clamped column was wrong.
- **The terminal-anchor bound applies to tone regions but not to sections or automation points**,
  so a marker past the end loads and silently piles onto the song's last instant. (Plan 42 records
  the same gap among the validation phases; this is the doing-entry. The three hand-written copies
  of `isValidGridPosition` this entry originally led with are gone — every validator now calls the
  one authority.)
- **The measure-keyed signature lookup linearly rescans** the authored list, contradicting the class's
  documented promise that construction precomputes indices — and five call sites ask it, some once
  per note.
- **The seconds-grid check rounds before anything bounds the magnitude**, so a hostile value makes the
  validity check itself undefined behavior; today the garbage result happens to fail the epsilon test.
- ~~A package can be a decompression bomb.~~ **Moved to `watch-items.md` (2026-08-10)** — accepted
  for now, since every package today is one the user chose and imported by hand. The trigger that
  graduates it, and the sizing logic so nobody re-derives it, are recorded there.
- **Timeline zoom persists through a 6-significant-digit formatter** while the sibling value the same
  reader parses uses an exact one for exactly this reason, so one zoom notch does not survive a
  reopen. The test picks a value that survives 6 digits, then asserts exact equality.
- **Save's "needs a destination first" is stated three ways** — the action silently no-ops, the
  availability predicate says Save is enabled, and the view re-implements the redirect. The deferral
  state machine already has the phase this belongs in.
- **`Session::loadSong` returns bool-as-failure at a `common/core` boundary**, so its one caller
  fabricates a wrong-domain error code. The conventions doc's own parameter-passing example
  demonstrates this signature, so fix both.
- **Four hot keystroke paths snapshot the whole history** to read one integer an accessor returns,
  building up to a hundred label strings per keystroke.
- **Every save re-deflates the whole backing FLAC at level 9** for a one-note edit, and the workspace
  is never pruned, so orphaned audio copies and abandoned tone documents accumulate in every future
  save.

- **Re-import EVERY package and project (2026-08-08, hard break; widened 2026-08-11).** The harmonic
  collapse replaced the `harmonic` and `touch` note fields with a single `harmonicNode`, and moved
  the pinch to `"attack": "pinch"`. The chart reader now **refuses** a document carrying either old
  key rather than ignoring it, so an un-reimported package fails to load with a message naming the
  fix — chosen over silent loading, which would have dropped every harmonic in the chart without a
  word. This affects the converter-sourced `.rock` corpus too, unlike the GP-only item below. The
  refusal in `chart_document.cpp` is a tripwire, not compatibility: delete it once the corpus is
  re-imported.
  **The legato ruling widened the same break (2026-08-11):** the attack tokens `"hammer"` and
  `"pull"` are gone, replaced in place by `"legato"` (the claim) and `"leftTap"`, and an unknown
  attack token is a read error — so every package written before that date now fails to load for this
  reason too, whether or not it carried a harmonic. Same remedy, same tripwire; warn the user before
  they open an old project.
  **The converter's own conversion notes need re-verifying before you trust them** (2026-08-10): the
  two recorded here — every natural harmonic gains a node equal to its touched fret (1960 of them in
  the packages measured), and pinch harmonics import as plain picks (5 notes corpus-wide, so a
  negligible loss) — describe the standalone converter, which lives in a **separate repository** and
  is not covered by this repo's audits. RockHero's own Guitar Pro importer has since moved on: a
  natural's node is the *snapped* value placed against the physical stop rather than the raw fret,
  and a pinch imports as `Pinch` carrying a node. Re-read the converter before re-running it and
  restate these two notes from what it actually does. Guitar Pro imports are unaffected either way —
  GP always records a harmonic fret (207 of 207 across 118 files).

- Re-import GP-derived projects/packages saved before 2026-08-06: charts imported before the
  scoop/trail-off model (commits 6b5c9894/4d33abbf) still embed moved-head slide-ins and
  windowless trail-off FHP tracks (e.g. the Van Halen import project that motivated plan 55), and
  charts imported before the sustain-policy rules 2 and 3 were narrowed (2026-08-06) still embed
  tails that a non-changing payload held open past the margin — a bend plateau or a trailing
  equal-fret hold waypoint — plus sub-beat chords whose unbent strings lost their tails while a
  bent partner kept one, and charts imported before the hold-waypoint placement fix (2026-08-07)
  have a stray fret-hand position baked in at every equal-fret hold waypoint, which shifts the hand
  mid-note on a tie chain that holds a fret and then trails off. That last one persists in saved
  projects rather than being re-derived on load, because `fret_hand_positions` is serialized into
  the chart document — re-importing is the only way to clear it. GP-import normalization applies to
  GP import only, so the converter-sourced `.rock` corpus is NOT affected — only saves that passed
  through the GP importer.
- Lane-caret keyboard navigation does not glide the viewport the way chart-caret navigation
  reveals its measure (2026-07-18): stepping a lane caret can walk it off-screen. Extend the
  caret-measure reveal to the lane caret's published seconds.
- Game F1 diagnostics overlay: the per-frame values line updates so fast it is unreadable while
  playing (witnessed during the 2026-07-16 milestone-0 soak — clock drift could not be read).
  Throttle the readout refresh (~2-4 Hz) and/or use fixed-width numerals so values stop
  jumping.
- Tone changes should switch SLIGHTLY before the tone region begins (~100 ms ahead?) so the
  transition still feels seamless for players who are a little out of time — needs evaluation and
  tuning.
- `g_capital_ink_fraction` (0.55, `tab_paint_core.cpp`) understates a capital's real ink by ~8.8%,
  so the T/S/P plates draw their letter at note heights where it spills ~0.19 px past the plate's
  inner height, and every slot mark's `tuck` floor is that much too small. Measured 2026-08-06:
  Verdana Bold's cap height is 1489/2048 em against a JUCE font height (ascent+descent) of
  1.215332 em, i.e. 0.5983 — 7.484 px measured at font height 12.5. Currently UNREACHABLE in the
  editor, because `TrackViewport` fixes the canvas so `note_height` clamps to 25 at every string
  count; it bites the game's 2D strips (plan 30) or any resizable row. Do NOT just write 0.5983:
  Verdana is only JUCE's Windows default sans
  (`juce_DirectWriteTypeface_windows.cpp:417`; Linux resolves Sans/Arial/Ubuntu), so a hardcoded
  ratio bakes in a platform-specific font fact. `GlyphArrangement` cannot measure it either — a
  `PositionedGlyph`'s bounds are the layout box, full font height rather than tight ink
  (`juce_GlyphArrangement.h:75`). Needs a per-typeface measurement or a deliberately conservative
  bound.
- Evaluate VST2 support feasibility.
- Automation lane "+" should look closer to the signal-chain "+" for visual consistency.
- Report the bgfx Conan-package issue upstream to conan-center — we rolled our own recipe because
  of a dependency clash with SDL3, but never filed the issue.
- Evaluate error-header organization: whether each error type gets its own header, or each domain
  gets a single domain-level errors header, to keep error classification organized project-wide.
- Generate a modern-C++-expert agent aware of the latest C++ features — it should know the actual
  current standard but give advice for the C++ version the project actually uses.
- Waveform drawing doesn't always finish before the project finishes loading — evaluate (may be
  fine to keep as-is).
- Playback should continue to the end of the grid even when the audio ends early (just silence
  after that point).
- Audit the project for position types that duplicate logic. (ToneGridPosition — the specific
  case that prompted this — is already removed from the code; only stale doc mentions remain.)
- Consider a plan for a basic suite of drop-in "standard tones" built only from plugins we ship
  with the project (so every user has them). Note (2026-07-11, supersedes the old "mandatory
  fallback" clause): 21-Q1 settled missing-plugin handling as refuse-to-start; standard tones
  would back the PINNED opt-in "play with default tones" option on that refusal (watch item in
  watch-items.md), never an automatic substitution.
- Editor: warn on export/publish to `.rock` when any tone's summed reported plugin latency is
  high (~10 ms+), so a charter cannot ship an unintentionally high-latency tone (21-Q2
  refinement: the game stays silent; the guard lives at authoring time). Data source: plan 21
  Phase 5's per-tone latency surfacing on the rig-load result. DEFERRED follow-on, recorded so
  it is not lost: a save-file flag marking high-latency tones so players could be alerted — a
  format change routed through plan 10 if ever adopted.
- Chord-box filling no longer tints its own chord's heads — accepted cost of drawing boxes before
  the notes (our board view has no depth writes); revisit only if it reads wrong in practice.
- Eyeball the un-witnessed Phase 4 technique renders — the connection triangles (drawn from the
  RESOLVED motion since 2026-08-11, so this now also checks that a hammer-on and a pull-off point the
  right ways after a neighbour edit), tap icons, harmonic heads, arpeggio brackets, tremolo wobble —
  on a legato/harmonic-heavy chart.
- Evaluate the engine ctor's unconditional auto-detect `initialise(1, 2)` followed by the
  editor's saved-route restore (a double device open at startup) — needs its own design and
  touches game startup too.
- Evaluate `handleAudioDeviceConfigurationRefresh` ordering: it detaches instrument monitoring
  before `dispatchPendingUpdates()` rebuilds Tracktion's wave list, churning playback contexts
  against a stale device list (crash hypothesis H3 from the 2026-07-14 reconnect investigation;
  much tamer now that the policy no longer reopens devices mid-pass).
- Evaluate gating `audioMeterSnapshot()` on `m_audio_device_configuration_refresh_pending` the
  way `currentInputDeviceIdentity()` already is (H4 from the same investigation; largely
  mitigated by the stable structural meter plugins).
- Decide UX for the audio-device failure popup vs. the input-calibration prompt appearing
  together (device disconnect while the calibration prompt is up) — modals currently just stack.
- Move section names in the 3D view to the upper-left corner instead of inline with the
  scrolling tab.
- Evaluate per-(project, arrangement) resume-marker records: the marker settings family is
  per-project today, so switching arrangements loses your spot in the previous one. Additive
  key-shape change in `EditorSettings` whenever it itches in practice.

## Found by the 2026-08-11 legato corpus re-import

- **The GP rhythm ladder stops at `64th`, so a `128th` value fails the whole import.**
  `gp_score_parser.cpp` (the rhythm-value ladder, ~lines 139-163) maps `Whole` through `64th` and then
  reports "unknown rhythm value: 128th". One file in the local corpus is rejected for nothing else
  about it — the score is otherwise ordinary — and the editor's own grid already offers 1/128, so the
  value is inside the domain the rest of the project accepts. Add the branch; verify the resulting
  `Fraction` against the ladder's existing pattern rather than by hand.

## Found by the 2026-08-10 five-area deep review

Five parallel reviews: the audio layer, the game side plus the shared 3D highway, the editor UI, a
whole-codebase consistency sweep, and a documentation-accuracy audit. What was severe shipped the
same day — the two torn gain smoothers, the teardown use-after-free, the camera framing a harmonic
where its head is not, the scrape-sends-the-hand-to-a-node predicate, a latent three-platform
`-Wfloat-equal` break, and a palette guarantee that did not exist. These are the remainder. Each was
verified against the code by the reviewer; re-verify before acting, since the tree has moved.

### Audio

- **Full device enumeration plus two heap allocations per meter tick** —
  `engine_live_input.cpp:294-312` calls `currentInstrumentWaveInput()`, which enumerates every wave
  input device and copies two `juce::StringArray`s, to re-derive a pointer that only changes on a
  device change. The engine already tracks device changes; attach the reader there.
- Minor: the 12 dB meter ceiling is inlined at `meter_reader.h:97` while the floor and clipping
  threshold are named functions. `i_live_input.h:22-26` describes a dry-tap ring buffer in the
  present tense before it exists. `multi_tone_rack.cpp` failure paths can leave a plugin in the rack
  tree but absent from `branch.chain` (error-path only, PLAUSIBLE whether reachable).

### Found while fixing the audio and package findings

- The `Arrangement` persisted-versus-prepared split (with the dead `difficulty` field folded in)
  and the `ScoringRuleset` version factory moved to
  `docs/plans/todo/design-calls-from-the-2026-08-10-review.md` — both are design decisions, which
  this file's own charter sends to a plan.
- **A tone span shorter than the de-zipper ramp is reachable — through the editor.**
  `makeToneGainEnvelope` clamps the crossfade to half the incoming span, so a span under 20 ms bakes
  one shorter than the 10 ms authority and under 10 ms drops below the 5 ms de-zipper.
  `validateToneTrackRules` imposes ordering and non-overlap but **no minimum spacing**, and the
  editor's own grid reaches the window: 1/128 spacing is 15.6 ms at 120 BPM and Ctrl precision
  snaps to a 1/960-beat grid, so two adjacent tone changes land inside it natively. A second route:
  `makeToneSchedule` floors the final span with `max(end, start)`, so a song length earlier than the
  last region's start yields a zero-length span. The consequence is a slightly smeared,
  already-inaudible switch rather than a click, which is why it was not chased.

### The 3D highway and the game

- **The windowing authority the project owns is applied to notes only.** `visibleEventRange` is
  used at the note sites in `highway_renderer.cpp` (the highway-named forwarders were deleted
  2026-08-10, and the chord groups now arrive windowed from the projection), but beats, fret-hand
  positions, tap onsets, and shapes are still scanned full-song every frame — about twenty sites
  by the later verification count; cite them by the loops' subjects, since the file's line numbers
  have moved twice. Worst is `windowSampleTimes`, called per shape rail and per window-following
  tail, each call allocating and walking every placement in the song then sorting and uniquing.
  `lower_bound`/`upper_bound` over the ramp interval, the shape `highway_window.cpp` already uses.
- **Per-frame allocation in the render path**: every drawer allocates fresh CPU batches each frame
  (~19 sites listed in the review) while `Impl` holds no scratch members — roughly two dozen
  allocations per frame, plus `makeHighwayTailSampleTimes` allocating and sorting up to ~430
  doubles per modulated tail. (The per-frame section-name copy-and-uppercase was fixed on the
  branch; the batches remain.)
- **The song-select menu has no viewport.** `rock-hero-game/ui/src/game/game.cpp` draws one row
  per library entry from a fixed origin. At 100 songs on 1080p, rows past 64 are off-screen, the
  key-hint footer never appears, and selecting song 80 puts the highlight bar at y = 1328 — the
  player navigates blind. Also one heap `std::format` per entry per frame. Windowing the rows adds
  code, but a fixed-height screen genuinely cannot state an unbounded list. (Plan 26's own
  follow-up notes the same gap; this is the doing-entry.)
- **CONFIRMED: `slide_state_at` returns no slide state when `note.fret <= 0`** and the rules do
  not refuse open-string slides, so an open-string note with waypoints draws a straight tail while
  its waypoint furniture still draws at the waypoint frets. Also every dev-session hot reload calls
  `camera.reset()`, so each save snaps the camera.

### Editor UI

- **~30 color literals outside the theme seam** (full census in the review) — a sweep once made.
  Whether the theme also grows font/size roles is a design call, moved to
  `docs/plans/todo/design-calls-from-the-2026-08-10-review.md`.
- Smaller: `busy_overlay` and `audio_device_failure_overlay` each compute their centered geometry
  twice and are near-duplicates of one another with a comment admitting the hand-maintained
  agreement; `signal_chain_view::paint()` re-walks `resized()`'s layout arithmetic. CONFIRMED:
  `keymap_editor_view.cpp` removes a key press before removing the stored indices, and
  `juce::Array::remove` compacts (invalidating them), so rebinding Redo to a chord it already owns
  can keep the chord it was asked to replace.
- **`RockHeroGame::Config` and `Game::Config` state the same six-field content contract twice**,
  kept in step only by the field-by-field copy in `rock_hero_game.cpp`. One direction is guarded
  (a field added to `Game::Config` and omitted from the designated initializer trips the strict
  compilers), the other is not (a field added to `RockHeroGame::Config` and never forwarded
  produces no diagnostic). One shared `GameContentConfig` embedded by both deletes the copy.

### Test debt from the 2026-08-10 verification pass

Behavior the branch changed correctly but did not pin; each is one focused case in an existing
suite:

- Three importer behavior changes in `gp_chart_builder.cpp` with no importer-level test: the
  strikeless-tap decision moved into the build loop (so chord-shape and FHP passes see the settled
  attack), the pick-slide carrier's capo floor (a capo ≥ 3 with a scrape carrier used to refuse
  the whole song), and the hopo direction deriving from the predecessor's RELEASED fret. One
  SECTION each in `test_gp_song_importer.cpp`, in the style of the out-of-range-fields case.
- Two pixel probes in `test_tab_paint_core.cpp`: the capo chip drawing before the FHP loop (the
  marker's digit must win the corner overlap), and `drawMuteIcon`'s size-floor removal (no mute
  ink outside the head's extent below ~11 px lane scale).
- One `TabLaneGeometry` case with a non-zero `bounds_x`, so the absolute-coordinate change stops
  being unobservable and a future caller cannot double-add the origin.
- `test_plugin_browser_window.cpp`: double-click now honors `add_enabled`; assert the refusal.
- A game-settings case asserting a setter's value is on disk before any explicit save (the shared
  options' zero save-delay is what fixed the silently-armed three-second timer).
- The audio-device failure overlay's Escape is now swallowed silently (it used to open Audio
  Settings); no test guards either contract.
- Test-data sweep: several editor test fixtures still spell rates as `48kHz` inside opaque status
  strings; inert, but they read against the spelled-out-hertz ruling.

### One rule in two places, across the tree

Most of what this section recorded was unified on the `code-review` branch (sixteen rules in
`1c2ec398` and its follow-ups) or in the 2026-08-10 verification pass that followed it. What
remains:

- **Four bare `1e-9` onset epsilons in `gp_chart_builder.cpp`** that should alias the named
  constant the way `highway_renderer.cpp` does.
- **`withinGrid` in `gp_chart_builder.cpp` restates `isValidGridPosition`**, papering over the
  real asymmetry: the file's two beat-to-position converters answer differently past the last bar
  (`advanceGridPosition` extends the final signature, `gridPositionForGlobalBeat` clamps the
  measure and emits an out-of-range beat). Making the latter extend like the tempo map deletes
  `withinGrid` and both call-site guards; short of that, call the authority.
- **Two payload-clip helpers for one rule**: `clipPayloadsTo` (importer) and
  `clipPayloadsToSustain` (editor) overlap, and the shift-slide junction hand-restates the
  slide-out clip immediately after calling the narrower one. Give the shared helper the slide-out
  clip behind its target parameter, or state why the builder deliberately owns a narrower rule.
- Evaluate: the importer's no-landing degradation path forces a DOWNWARD trail-off even when the
  notated glide direction is known (consistent with the pre-existing no-landing path, so a
  deliberate change would touch both).
- Smaller: two remaining copies of `getIndexOfDevice`
  (`test_audio_device_settings.cpp`, `test_engine.cpp`).
- Structural: `TempoMap` has no validator of its own, so its rule set lives only in the *package*
  feature while the header documents its constructor as taking "already validated" input with nothing
  to call. Not yet a duplication, but it is the shape that becomes one.

### Documentation claims that are wrong

The header-claim sweep on the branch fixed this section's original twelve entries with their
code. Two residues survive it:

- `highway_view_state.h` documents one tap-fret quantization in two places with two different
  treatments (one `ceil`'d, one raw) — settle which the producer really applies and say it once.
- `GridPosition::offset`'s `[0, 1)` range is enforced only by the validators while several
  `grid_arithmetic.h` functions silently depend on it; the header should state the precondition
  (or the functions should assert it), so the dependency stops being implicit.
