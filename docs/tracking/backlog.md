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

## Found by the 2026-08-15 emphasis-axis reviews

- **The note-token parse/format pairs are stated twice each, and there are now three of them.**
  `attack`, `mute` and `emphasis` each spell their tokens once in the reader's if-chain and again
  in the writer's switch, with only a round-trip test standing between a writer-side rename and
  silent divergence. `chart_tokens.h` already owns exactly this parse/format-pair shape for grid
  positions and beat fractions, so the third instance is the one that earns lifting all three into
  it. Deliberately not done with the emphasis change: it would have pulled two unrelated enums
  into that diff.
- **The harmonic/touch tripwire is weaker than the emphasis one it was the model for.** It tests
  `readOptionalString(...).empty()` and `tryReadDouble(...)`, both type-dependent, so
  `"harmonic": 1` or `"touch": "12"` slips through where the newer `!Json::value(...).isVoid()`
  form catches any type. Harmless today (the real old keys were a string and a number), but the
  older tripwire should be rewritten in the newer shape while both still exist.

## Found in the 2026-08-15 highway visual pass

Both sighted by the user against the technique-showcase package; each carries a ruling.

- **A pick slide's turnarounds are abrupt where an ordinary slide's are eased.** A scrape with
  direction keyframes kinks at each one, and its slide-out draws unlike an ordinary slide-out.
  Ruling: a pick slide's keyframes should navigate smoothly exactly as a pitched slide's do, and
  the terminal should draw the same shape an ordinary slide-out draws. Check whether the easing
  authority (`highwaySlideEaseWeight`) is simply not reached on the scrape path rather than
  needing a second easing rule — a scrape is unpitched, so the question is which easing a
  *travel* gesture takes, not whether pitch eases.
- **The two notes of a chord wobble in OPPOSITE directions under vibrato**, because
  `highwayBendInverted` answers per note from that note's own lane, and a chord straddling the
  middle of the stack therefore splits. Ruling (user, 2026-08-15): the direction belongs to the
  ONSET GROUP, not the note — a majority vote over the group's members decides one direction for
  all of them, counting members on the strings below G against those on G and above, with ties
  resolved to the same side the majority-high case takes. **Confirm the orientation against the
  user's own examples before building** (their colour language reads G+D as one way and G+D+A as
  the other, which pins which side "above" names), and check whether a group-wide direction can
  push a member into `highwayBentNoteY`'s saturation clamp — the per-note rule exists precisely
  to pick the roomier side, so overriding it is what could make a bend hit the board edge.

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
  **Four rule tightenings briefly widened it again on 2026-08-20, and W4 closed that class the
  same day:** the fret cap (30 → 24), the open-string slide rules, the `capo + 1` slide floors,
  and (then) E25's muted tail are all REPAIRS of the one normalizer, so a project saved under the
  older rules opens, is repaired, reports what changed and where in a one-shot notice, and opens
  dirty — it no longer refuses. The same-string ring bound (40-Q2-B, `OverlappingTail`) joined that
  list on 2026-08-22.
  **The note-sustain model added a THIRD token-level break on 2026-08-22:** `notes[].sustain` is
  required and must be strictly positive, because a note's ring is now the actual duration the
  string sounds and no repair can invent one. Every package written before that date is missing the
  key on its tail-less notes — the old writer elided it rather than writing a zero — so a real one
  refuses on the MISSING-key path, which is why that message (not only the positive-sustain rule's)
  names the fix. Same remedy as the two below — re-import — and the same tripwire.
  Only these token-level breaks refuse, because a reader cannot repair a key it does not recognize
  (or one that was never written) without guessing; they keep their re-import remedy and their
  tripwire.
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
  equal-fret hold keyframe — plus sub-beat chords whose unbent strings lost their tails while a
  bent partner kept one, and charts imported before the hold-keyframe placement fix (2026-08-07)
  have a stray fret-hand position baked in at every equal-fret hold keyframe, which shifts the hand
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

- ~~**The windowing authority the project owns is applied to notes only.**~~ — **FIXED 2026-08-24**
  in `highway_renderer.cpp`: every whole-song scan in `draw()` is now bounded, each keeping its own
  per-item test so the bound is a tight superset and never a second filter. Beats, section labels,
  tapped chord boxes and the tap strike glow take the plain `lower_bound`/`upper_bound` clamp; the
  two shape passes share one `visibleEventRange` over a new `shape_prefix_max`; the settled
  hand-window collection and the window light's motion dim bound on ascending arrivals plus
  `max_fhp_ramp_seconds`; and the five tap-light scans go through one `litTapOnsetRange` — the
  tapping hand's `visibleEventRange`, over a new `tap_end_prefix_max` (the prefix maximum of the
  light paths' end times) and `max_tap_ramp_seconds`. `handWindowMovesWithin` took the bound its
  `windowSampleTimes` neighbour already had. **The `windowSampleTimes` half of this entry was
  already STALE when written**: that function has filled the caller's buffer and binary-searched
  its own start since the fhp-window-motion work — it neither allocates nor walks the whole song.
  Still full-song by design and correctly so: the strike glow's forward spacing walks, which look
  PAST the visible run for the next same-geometry strike and stop at the clamp horizon.
- ~~**Per-frame allocation in the render path**~~ — **FIXED 2026-08-24**, with two named exceptions.
  Every fresh vector inside `draw()` now lives in `FrameScratch` and is cleared in
  `clearForFrame()`: the ten sequential furniture passes share one cleared-on-handout batch pair
  per vertex layout (`colorBatch()` / `texturedBatch()`, which `drawOverlayRects` takes too), and
  the hand windows, window-light slices (four parallel arrays folded into one record), bracket
  batch list, box draw list, strike-glow onsets, accent-glow columns, and the modulated tail's
  wobble times, samples, lifts and shades are members. `pushTailGlowSegment` takes its column
  buffer as a parameter, so the per-ribbon-SEGMENT allocation is gone. The tail-shade smoothing is
  now O(S) — three running sums plus a two-cursor window, since the tent weight is linear in a
  sample's z; the window SET is bit-identical to the old outward walks and only the float summation
  order changed. Two separate randomized cross-checks, not one: 20,000 tails showed 0 window-set
  mismatches, and a 4,000-tail run measured the value divergence at worst 1.5e-10, against a 1/255
  colour step of 3.9e-3. **The two exceptions:** `makeHighwayTailSampleTimes` returns its list by
  value, so `sample_times` is still one allocation per MODULATED tail per frame — that branch is
  gated on bend, vibrato, slide, tremolo or a moving open band, not on the tail being lit — until
  that core seam fills a caller's buffer the way `windowSampleTimes` does; and each `BracketBatch`
  still owns its own two vectors, so a visible arpeggio posture string allocates. Untouched and
  still open from the same review: `StringLaneStyle` is derived per visible note with six of seven
  fields unread, and slide-run boundaries are recomputed per tail sample. In the 2D lane, the
  per-note bracket rescan and the per-chip HarfBuzz shaping are still per-song at minimum zoom,
  and the shapes' visible range still scans from the song start for want of a prefix maximum of
  span ends — exactly the table the highway just gained.
- **The song-select menu has no viewport.** `rock-hero-game/ui/src/game/game.cpp` draws one row
  per library entry from a fixed origin. At 100 songs on 1080p, rows past 64 are off-screen, the
  key-hint footer never appears, and selecting song 80 puts the highlight bar at y = 1328 — the
  player navigates blind. Also one heap `std::format` per entry per frame. Windowing the rows adds
  code, but a fixed-height screen genuinely cannot state an unbounded list. (Plan 26's own
  follow-up notes the same gap; this is the doing-entry.)
- ~~CONFIRMED: `slide_state_at` returns no slide state when `note.fret <= 0` and the rules do
  not refuse open-string slides~~ — **rules half FIXED 2026-08-20** (user rule: an open string
  cannot slide — nothing is pressed to travel): the normalizer (`normalizeChartNote`) drops a
  fret-0 glide or trail-off and the validator asks that fixpoint — a scrape's start lifts above
  the capo instead (W9-J) — so the form `slide_state_at` ignores can no longer exist in a valid
  chart. Still open from the same
  entry: every dev-session hot reload calls `camera.reset()`, so each save snaps the camera.

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
  strikeless-tap decision moved into the build loop (so the hand-window pass sees the settled
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
- **Two payload-clip helpers for one rule**: `clipPayloadsTo` (`chart_presentation.h`) and
  `clipPayloadsToSustain` (`chart_rules.h`) overlap, and the importer's shift-slide junction
  hand-restates the slide-out clip immediately after calling the narrower one. Both now live in
  `common/core`, so the consolidation is a one-file edit: give the shared helper the slide-out clip
  behind its target parameter, or state why the presentation trim deliberately owns a narrower
  rule (it is still choosing where its end goes when it clips, which is the current answer).
- **The tone automation lane's state-snap math is stated twice** (found by the 2026-08-23
  tone-automation review; the curve-evaluator half of it was fixed 2026-08-25 by the lane anchor,
  which put the whole evaluation — hold-vs-ramp included — behind one
  `toneAutomationCurveValueAtSeconds` both surfaces call). What remains: the state-snap math
  `k/(count-1)` is duplicated between `snappedLaneValue` (`tone_handlers.cpp`) and
  `ToneAutomationLanesView::snappedValueForLane`. Both restate the "two states or more counts as
  stepped" threshold that `tone_automation_curve.cpp` states once, inside `discreteValueCount`.
  It is a pure function of the lane's parameter, so one shared authority the view calls removes it.
  What invites the restatements is upstream: `is_discrete` is derived from
  `discrete_value_count`, yet is carried beside it through the parameter descriptor, the lane
  view state, and the pointer event, leaving every consumer free to re-derive it its own way.
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

### Tab lane plate geometry (measured by the 2026-08-12 palette review)

- The plate rim is a centered 1.00 px stroke on an unsnapped rect at every note height ≤ 16.36,
  so the same grey renders anywhere between L* 30.6 and 53.6 depending on sub-pixel phase — a
  23 L* note-to-note swing, twenty times the entire rim-value decision. Fix: snap the plate rect
  (or the rim) to whole pixels below that height.
- At the smallest letter-drawing size (note height 11.70) the T's antialias tail merges into the
  rim and both plate polarities read "II". The under-measured ink fraction that made this worse is
  gone — `TabLaneFont::inkHeight` now measures the figure's real outline, so the letter is
  suppressed at 0.598 of the font height rather than 0.55 — but that is still short of the ~0.75
  the sighting wanted. If it still merges, give letters the chip's ink-to-rim clearance rather than
  inflating the measured ink, which is now a real number and not a knob.

### Highway world units: express the board in its own two units

Every world length in `HighwayMetrics` is an absolute — `first_fret_distance` 1.1,
`string_distance` 0.35, `note_half_width` 0.48, the camera's 5.0 and -2.5 — so nothing in the code
says which numbers are the LOOK and which are merely the units. The look lives in the ratios
between them, and today every one of those ratios is computed by hand each time it is needed.

**The absolute scale is not arbitrary, and this entry is not a renormalization.** Those values are
Charter's, at Charter's scale; 1.1 is the fret's size in that inherited coordinate system (its 1.2
narrowed so heads fill more of their slot). Rescaling everything so a fret reads 1.0 would sever
each constant from the source it can still be compared against, and would buy nothing the change
below does not buy better.

The change is to state the PROPORTIONS in the code and let the anchors carry the scale:

    note_half_width = first_fret_distance * 0.436     // instead of a bare 0.48

Then an anchor's value becomes genuinely inert — 1.0 or 1.1, it no longer matters, because moving
it rescales its axis coherently — where today an anchor is arbitrary *and* load-bearing by hand.

**Two anchors, not one, and this is the part to get right.** The fret governs X; string spacing
governs Y. On a real instrument they are independent (fret spacing follows scale length, string
spacing follows nut and bridge width), so expressing a Y quantity in frets asserts a coupling that
does not exist — and it breaks outright under roadmap 25-Q1's variable fret width, where "0.318
frets" stops naming one distance. The measurements already work this way: the head art is **0.288
frets wide** and **0.471 string-spacings tall**, and that second number is exactly the ratio the
reference-proportion work measures. Each length belongs to the axis it actually spans; the
fret-to-string ratio is then the one explicit board-aspect parameter rather than an accident.

Two traps:

- **Not everything is a world length.** NDC heights (`board_anchor_ndc_height`), pixel quantities
  (`g_tail_pixels_per_sample`), seconds, and dimensionless fractions have no anchor and must stay
  absolute. A misclassified one does not fail to compile — it silently shifts the look.
- Texel-space constants are already relative (`headArtTexelWorld` derives from `note_half_width`),
  so they follow automatically and must not be converted by hand.

Acceptance test that makes this safe: expressing a value as its own anchor times its own ratio
changes nothing, so the frame must render **pixel-identical**. Capture before and after and
compare framebuffers; any difference beyond float rounding means a constant was misclassified or a
ratio was rounded too hard.

Do it when the fret axis is otherwise quiet. Roadmap 25-Q1 (a neck that compresses toward the
body) is the natural moment to decide it, since a variable fret width is exactly what makes the
two-anchor split matter rather than being a stylistic preference.

## Found while replacing the grid readout's snap-off mark (2026-08-24)

### The transport strip's combo boxes are outside the editor's one color seam

`EditorTheme` is described as the single place editor colors are decided, but the surface the grid
readout's snap-off strike lands on is not one of them. That readout is a `juce::ComboBox`, and its
fill `0xff263238` and text `0xffffffff` come from JUCE's own dark scheme. Citations below are
relative to `external/tracktion_engine/modules/juce/modules/juce_gui_basics/`: the two values are
`widgetBackground` and `defaultText` in `lookandfeel/juce_LookAndFeel_V4.cpp:91` (positional scheme
fields, whose enum order is at `lookandfeel/juce_LookAndFeel_V4.h:55-68`), routed to
`ComboBox::backgroundColourId` and `ComboBox::textColourId` at `juce_LookAndFeel_V4.cpp:1390-1391`,
and pushed into the value label at `widgets/juce_ComboBox.cpp:405`. Nothing in the project overrides
either: `EditorTheme` (`rock-hero-editor/ui/src/shared/editor_theme.h:26`) names no combo-box role,
and the `primary_text` white it does name (`:111`, `0xffffffff`) matches the digits only by
coincidence of that scheme.

Nothing to fix while there is one theme, and the strike itself is already safe — it reads its ink
off the box, so the two whites cannot silently diverge. **The trigger is user-selectable themes**:
at that point the strip's chrome would follow JUCE while every editor-drawn surface follows
`EditorTheme`, and the readout would stop matching the timeline beside it. If a theme role is wanted
for this mark when that happens, name it for the claim the mark makes — *a datum that is displayed
but does not bind* — rather than borrowing `primary_text`, which would put the "same ink as the
digits" rule in two places that have to agree by hand.

## Backing clip: pin autoTempo/autoPitch off at arrangement load

Found 2026-08-25 during the clock-boundary work. `Engine::setActiveArrangement`
(`rock-hero-common/audio/src/engine/engine_song_audio.cpp`) pins the backing clip's sync type to
`syncAbsolute` and proxying off, but never pins `autoTempo`/`autoPitch` off. Tracktion enables
`autoPitch` on insert whenever the source file's loop metadata carries a root note
(`tracktion_ClipOwner.cpp:283`), so a user asset with loop metadata gets its backing track
time-stretched and pitch-shifted to the edit's tempo and pitch sequence -- the exact hazard
`syncAbsolute` exists to prevent, and RockHero's tempo authority makes the edit sequence
meaningless for audio. Today that path can also abort outright in debug: the vendored
`tracktion_WaveNode.cpp:2138` (twin at `:2152`) passes `std::move(timeStretchReader)` and
`timeStretchReader.get()` in one argument list -- unsequenced, and MSVC nulls the source before
the getter runs, tripping the assert at `:984` (hit for real by a loop-annotated test fixture;
documented in `test_engine.cpp`'s load test). The fix is two property pins beside the existing
`syncAbsolute` pin; the vendored bug then stays unreachable from our product code.


## Automation lanes: the band color is stated twice

Found 2026-08-27 by the hollow-anchor build. The lanes' band color lives in
`track_viewport.cpp` (the canvas the viewport paints under the lanes) and now a second time in
the anchor mark's opaque center fill (`tone_automation_lanes_view.cpp`), which must mask the
curve line by matching that canvas. Both sites name the layering contract in comments, but two
places agreeing by hand is the registry's oldest defect shape. The one-authority fix is a theme
role of its own (an automation-lane-background role in `EditorTheme`) consumed by both sites —
an `EditorTheme` change, so it needs the user's sign-off rather than a drive-by edit.

## 2026-08-27 ruleset walkthrough

- **Count the dropped strum-direction marks at GP import** (D7 ruling,
  `docs/plans/in-progress/chart-ruleset.md`): the beat-level `Brush` element (with its
  `Direction` child) and `PickStroke` currently vanish without a trace. Direction stays
  deliberately out of the chart record, but the drop gets a conversion count per the Feedback
  precedent — one aggregate notice per kind, mirroring the trill notices' style. Small importer
  change; the count also prices `docs/plans/todo/strum-direction-support.md` if that distant
  item is ever picked up.

- **Hoist the technique-toggle test fixture** (2026-08-28, from the T/S/P build): the new
  `AttackToggleFixture` in `test_chart_technique_toggles.cpp` duplicates the shape of
  `SilentHoldFixture` in `test_chart_silent_hold.cpp`, and the file's ten pre-existing tests
  still spell out ~14 lines of controller setup each. Hoist one fixture into
  `chart_editing_fixture.h` and convert both suites (~150 lines, mechanical).

- **Census rig: flip the [D4] fold-in to the stored ring** (2026-08-28, from the stage-2b
  classification-stream ruling): `test_corpus_census.cpp:758` still folds carried strings in off
  `presented[ringing].sustain`, measuring a reading production no longer has — the walk's fold-in
  reads the STORED ring. One-line change (`presented` → `saved`), deliberately left out of the 2b
  verification run so the arpeggio movement stayed attributable to the ruling alone; flip it and
  run the census on its own so the [D4] carried-fret-distance histograms can be re-read against
  the stream the rule actually uses.

- **Close the landing-to-next-slot inert-hold window** (2026-08-29, from the landing-split
  rebuild): a silent hold authored between a span's landing and the walk's next slot is judged
  against the PREDECESSOR (already closed at the landing) rather than the standing successor, so
  it publishes nothing and sweeps. Far smaller than the old glide-wide dead zone (the amendment
  closed that one — a mid-travel hold now rides the covering span's growth law), but a real
  authoring pocket. Fix shape: `settle_landing` runs before claims attach, so the claim's slot
  should see the successor standing; verify ordering at the slot loop and pin with a test.

- **Rebuild the two 2026-08-30 floor lights when their questions are answerable.** Both were
  built, sighted, and **TABLED the same day (user)** — removed from the tree rather than left
  behind a switch, because a shipped feature nobody can rule on is a maintenance cost with no
  reader. Nothing about either survives in the code except what stands on its own (below); the
  built versions, their derivations, their tests and their sighting rigs are in the history at
  `eeb26eca` (both features) and `9731dcee` (the fixes and the hue rig).
  - **The FHP silence fade** — the fretting hand's backlight going out through a left-hand rest
    of a quarter note or more and returning ahead of the next statement, led by `marginBefore`.
    Revisit **after the derivation plan lands**: the rule's threshold and its lead are both
    musical, and what "left-hand information" means is exactly the kind of closed list that plan
    is settling. Two open questions rode it and are still open — whether a picking-hand TAP keeps
    the light lit (it shipped true, conservatively; a tap says nothing about the fretting hand),
    and whether the lit stretch should be measured from the STORED ring rather than the presented
    tail, which is decisive at a quarter-note threshold. **The span-fold-order fix in `9731dcee`
    dies with the removal** — folding a statement's covering spans BEFORE measuring the silence
    in front of it erased every rest that ended on a chord; a rebuild that re-derives this must
    measure first and fold after, and `9731dcee`'s test is the discriminator.
  - **The harmonic node light** — a floor glow under every note whose harmonic node lies on the
    neck. Removed with its whole hue-sighting rig (F9 cycling three candidates). Its one open
    question is what the light should be COLOURED: one colour for every harmonic (the FHP
    family's light blue shipped as the primary), the chord box's dimmer teal, or the note's own
    string — white excluded by signed convention as the picking hand's. Also unruled: whether the
    light means "a harmonic is here" (as built) or strictly "the left hand acts here". What
    SURVIVES the removal and stands on its own: `highwayHarmonicMark` (the lifted predicate the
    head cell reads) and `harmonicMarkFootprint` — a harmonic's fret-span line stays NODE-centred,
    which was the user's own sighting and is right with or without a light over it.

- **Merge or keep the ordered legato/claim sweeps** (2026-08-30, from the rule-11 rebuild): the
  documented reason the two normalization sweeps were order-dependent ("flattening a claim
  changes an articulation, and spans are keyed by articulation") dissolved when continuation
  went position-only — sweepUnjustifiedLegato writes only note.attack, and both Legato and Pick
  are fretting-hand non-silent, so the spans judged by sweepInertClaimedStops are identical
  either way. The three comments are already corrected (order = reading order, not a condition).
  Open cleanup: keep the order as harmless, or merge the two sweeps into one pass.

## 2026-09-01 bracket law (the clip/tail mini-seam) — CLOSED 2026-09-04

- **`ChartShape::covers_travel` had NO READER — DELETED with the tail law (stage C, 2026-09-04).**
  It was [D2] amendment 1 to C3, the ink-ownership rule: a span covering a member's glide owned no
  member ink, because a standing mark and a travelling ribbon stop saying the same thing. The
  entry's option (b) — "rule that a travelling span IS exempt" — is now answered BY CONSTRUCTION
  rather than by a ruling: the tail law never hides a ring that STATES something, so a travelling
  member is protected by the law's own PRESENCE disjunct and no span-level exemption exists to
  give it. With the last reason to keep the field alive gone, option (a) was taken: the field, its
  derivation arm in `deriveChartShapes`, and the assertions in `test_chart_shapes.cpp` and
  `test_chart_presentation.cpp` are all gone.

- **Cover the [D2] derived+fronting overwrite arm in test_chart_projection.cpp** (2026-09-02): the projection assigns Revealed by authorship first and the [D2] displaced-digit arm OVERWRITES it to Posture for a tap fronting its bracket (chart_projection.cpp ~:373-377). The shipped suite covers authored+fronting (Posture) and derived+mid-span (Revealed) but not derived+fronting — the sighting-reel generator proved the law correct there against the real derivation; a permanent unit test should pin it (the reel fixture in the generator record is a ready-made template).

## Highway repeat-box membership reads the drawn extent (found 2026-09-04, UNVERIFIED population)

`highway_view_state.h` (`lies_in`, ~:887) asks "does this onset lie in this span" — feeding
repeat-box identity and `arpeggio_mark` — against `drawn_end_seconds`, while the 2026-09-04
musical-close ruling moved every other membership question (`SpanCover`, the hold, the bracket
clip, the arrivals tap window) to the close. An onset inside the final display margin would be
judged outside its own span. The population may be empty: a sounding onset that RODE the span is
a statement, and the last statement floors the trim at itself, so the only candidates are onsets
in the gap of a span whose closing head sits more than a margin past its last strum (a tap under
the span's final margin is the concrete suspect). Verify with a fixture before changing the read;
if real, the fix is one field name (`close_seconds`) plus a pinned test.

## Modulated rested tails miss the window-anchored tessellation (found 2026-09-06)

The hidden-tail curtain gradient gets its flash-proof sampling only on the plain branch:
`highway_renderer.cpp`'s window-anchored sixteenths run inside the `!modulated &&
!open_band_moves` path, so a LANDED rested tail that bends, vibratoes, slides, or rides a moving
hand window falls to the arc-length branch, whose sample grid is anchored to `tail_from..tail_to`
and slides under the steep power curve every frame — the same re-tessellation class the
window-anchored comment records as having pulsed visibly before. Found by the 2026-09-06 verify
pass; pre-existing (the in-flight phase no longer carries a gradient at all, so exposure shrank).
Fix shape: inject the window-boundary sixteenths into the modulated branch's sample positions for
`rested && landed` tails, or verify the arc-length density already oversamples enough to hide it.
