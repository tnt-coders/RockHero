# Backlog

Small fixes and evaluations to do when there's time — short entries, not plans. Counterpart to
[watch-items.md](watch-items.md): backlog items you *do*, watch items you *monitor*. Anything
that needs a design or multiple steps belongs in a `docs/plans/todo/` plan instead. Delete an
entry when it's done — git history is the record.

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
- **A plugin-state undo entry is applied against whichever tone is audible.** Once playback crosses
  into a region on another tone, the entry's preflight fails and every later Ctrl+Z hits the same top
  entry, so undo is dead for the rest of the session. The entry captures a tone name but uses it only
  for the label. Fix wants the tone ref on the edit (as the remove edit already carries) plus a "make
  this tone audible" step on the edit context. **NEEDS A RULING: may Ctrl+Z change what the user is
  hearing, or must it refuse loudly?** Either way a refused undo must not leave the stack silently
  dead.
- **The song-document builder writes while it validates.** Its per-arrangement loop writes the chart
  file and copies audio in the same iteration that validates, so a failure on arrangement N leaves
  N-1 rewritten and its audio copied while `song.json` still describes the old state. Its own comment
  claims the opposite and is true only within one iteration.
- **Arrangements sharing one external backing file each get a copy.** The dedup map is consulted after
  the import and keyed on the destination, so three arrangements pointing at one external file produce
  three identical copies. It self-corrects on the second save, which is why no test caught it.
- **Two authorities for which meter governs a moment.** The seconds-keyed lookup reads a column filled
  through a CLAMPING resolver, so every signature change past the terminal anchor collapses onto the
  last anchor's time and disagrees with the measure-keyed lookup. Bounding a signature's measure by
  the terminal anchor in validation is the fix; deriving one from the other is precision-fragile
  exactly at a meter-change downbeat, and that yield is the signal the clamped column was wrong.
- **`isValidGridPosition` is hand-written in three validators**, one already drifted stylistically.
  Related: the terminal-anchor bound applies to tone regions but not to sections or automation points,
  so a marker past the end loads and silently piles onto the song's last instant.
- **A second parser for the `measure:beat` grammar** in the package format TU, existing only to be the
  chart token parser restricted to a zero offset. Deleting it removes ~45 lines.
- **The measure-keyed signature lookup linearly rescans** the authored list, contradicting the class's
  documented promise that construction precomputes indices — and three callers ask it once per note.
- **The seconds-grid check rounds before anything bounds the magnitude**, so a hostile value makes the
  validity check itself undefined behavior; today the garbage result happens to fail the epsilon test.
- **`isSafeRelativePath` is implemented twice, across a library boundary** — the path-escape rule that
  keeps a package-supplied reference inside the workspace exists at
  `rock-hero-common/core/src/package/rock_song_package_format.cpp:69` and again at
  `rock-hero-common/audio/src/shared/audio_path_util.cpp:88`, with a third partial variant
  (`isSafeZipEntryName`) at `rock-hero-common/core/src/package/rock_song_package_read.cpp:89`. Two of
  the three agree today only by luck, and they are the security boundary for a downloaded package: the
  core copy tests the whole path for a colon while the audio copy tests each part, and only the audio
  copy rejects `is_absolute()`. Unifying DELETES two copies — the rule is workspace-relative-path
  safety, so it belongs in the public `package/workspace_paths.h` that both libraries already include,
  not in a private format header the audio library cannot reach. That unreachable-header detail is
  precisely why the second copy was written, which makes the placement the root cause rather than the
  duplication.
- **The ZIP-safety check narrows package-supplied text** with the conversion this codebase elsewhere
  documents as throwing on MSVC outside the active code page, from inside a typed-failure API. Five
  error-message constructions do the same while building a failure report. The core
  `isSafeRelativePath` above shares the defect (`path.string()`), and the audio copy does not — one
  more reason the surviving copy should be the audio one's shape.
- **The package reader silently accepts a present-but-wrong-type number.** `startOffset`
  (`rock_song_package_read.cpp:232`) and an automation point's `shape` (line 682) both read through
  `tryReadDouble(...).value_or(0.0)`, so a string or object where a number belongs loads as 0.0 — for
  `startOffset` that silently shifts the whole backing track against the score. The chart reader was
  fixed this session to REFUSE a present-but-wrong-type field for exactly this reason; the same rule
  belongs here. Neither value is bounded either, so a non-finite `startOffset` reaches `TimeDuration`.
- **Two sections may share one grid position.** The sortedness check at
  `rock_song_package_read.cpp:459` uses `<` rather than `<=`, so a document naming one instant twice
  loads and every later question of which section governs that moment answers arbitrarily.
- **A package can be a decompression bomb.** `extractZipToWorkspace` caps neither the total extracted
  size nor the expansion ratio, so a small `.rock` from an untrusted source can fill the disk. A cap
  ADDS code, which is the signal to check the design first — but here the reader genuinely is the
  trust boundary and there is nowhere cheaper to put it, so the yield looks real. **NEEDS A NUMBER
  FROM THE USER**: the ceiling is a policy choice, not something to invent.
- **Four orphaned or duplicated comments in the package reader**, each describing code that no longer
  exists beside it: `rock_song_package_read.cpp:46` documents a deleted anchor-address struct above
  `findSongDocument`, lines 87-88 and 383-384 are each a leftover comment stacked on the real one, and
  line 985 documents a deleted part-spelling converter immediately above the namespace close. Same
  class as the orphans already fixed in the editor view this session.
- **`Arrangement::difficulty` and `Arrangement::audio_duration` are never persisted or computed** —
  the reader default-constructs both (`rock_song_package_read.cpp:838`, `:840`) and nothing in the
  package layer writes them. They are runtime-derived fields sitting in a value type whose other
  members are persisted truth, so a reader cannot tell which fields a package actually carries.
  Confirm nothing outside the package layer fills them, then delete or relocate them.
- **A dead guard whose name also lies** (`startsWithParentTraversal` is an `any_of`), with a subsumed
  disjunct at both call sites.
- **Timeline zoom persists through a 6-significant-digit formatter** while the sibling value the same
  reader parses uses an exact one for exactly this reason, so one zoom notch does not survive a
  reopen. The test picks a value that survives 6 digits, then asserts exact equality.
- **Two sample-rate integrality epsilons, identical constants, different units** (kHz versus Hz), so
  the settings combo and the menu-bar status already disagree about 44100.5 Hz.
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

- **Re-import EVERY package and project (2026-08-08, hard break).** The harmonic collapse replaced
  the `harmonic` and `touch` note fields with a single `harmonicNode`, and moved the pinch to
  `"attack": "pinch"`. The chart reader now **refuses** a document carrying either old key rather than
  ignoring it, so an un-reimported package fails to load with a message naming the fix — chosen over
  silent loading, which would have dropped every harmonic in the chart without a word. This affects
  the converter-sourced `.rock` corpus too, unlike the GP-only item below. The refusal in
  `chart_document.cpp` is a tripwire, not compatibility: delete it once the corpus is re-imported.
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
- Eyeball the un-witnessed Phase 4 technique renders — hammer-on/pull-off/tap icons, harmonic
  heads, arpeggio brackets, tremolo wobble — on a legato/harmonic-heavy chart.
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

## Found by the 2026-08-10 five-area deep review

Five parallel reviews: the audio layer, the game side plus the shared 3D highway, the editor UI, a
whole-codebase consistency sweep, and a documentation-accuracy audit. What was severe shipped the
same day — the two torn gain smoothers, the teardown use-after-free, the camera framing a harmonic
where its head is not, the scrape-sends-the-hand-to-a-node predicate, a latent three-platform
`-Wfloat-equal` break, and a palette guarantee that did not exist. These are the remainder. Each was
verified against the code by the reviewer; re-verify before acting, since the tree has moved.

### Audio

- **Audible playback position is computed twice, byte for byte** — `engine_transport.cpp:254-268`
  (`Engine::position()`, feeding the editor cursor) and `engine_clock.cpp:80-96`
  (`publishAudibleTimeNow()`, feeding the clock snapshots the game frame loop and 3D preview read).
  They agree today. Change one and the editor cursor and the highway report different song times for
  the same instant — the dual-timing drift the architecture doc rates High. One private
  `Impl::audiblePositionNow()` deletes ~8 duplicated lines.
- **Playback speed lives in two unlinked places** — `Impl::m_playback_speed`
  (`engine_transport.cpp:180`) and the clock's published rate, set once in the constructor
  (`engine.cpp:125`) and never republished. Latent only because the port rejects every value except
  1.0. The day practice speed lands, `setPlaybackSpeed(0.75)` leaves the clock publishing 1.0, the
  extrapolator advances view time at 1.0x against audio at 0.75x, and the highway judders and snaps
  back on each republish. Scoring would take speed from a third path
  (`game/core/src/scoring/timing_window.cpp:56`). Fix deletes the field: publish through the clock
  and read the rate back from the snapshot.
- **The de-zipper constant restates a number and gets it wrong.**
  `tone_branch_gain_plugin.cpp:9-11` hard-codes a 5 ms smoothing ramp and claims it stays at or
  below a 5 ms baked crossfade; the authority is `tone_schedule.h:64-71` at 10 ms. The invariant
  still holds, so this is a false fact rather than a live bug — but `makeToneGainEnvelope` clamps the
  crossfade to half the incoming span, so a sub-10 ms tone span bakes a shorter crossfade than the
  de-zipper, which is the failure the comment claims to prevent (PLAUSIBLE, needs an extreme tempo).
  Derive the local constant from core and delete the literal.
- **A redundant path-escape clause** at `engine_live_rig.cpp:700`: `isCanonicalToneDocumentRef`
  already constrains the string to `tones/<canonical uuid>/tone.json`, a shape that cannot contain a
  traversal, root, or drive letter, so the `isSafeRelativePath` clause beside it cannot reject
  anything. Deletes a clause.
- **Full device enumeration plus two heap allocations per meter tick** —
  `engine_live_input.cpp:294-312` calls `currentInstrumentWaveInput()`, which enumerates every wave
  input device and copies two `juce::StringArray`s, to re-derive a pointer that only changes on a
  device change. The engine already tracks device changes; attach the reader there.
- Minor: the 12 dB meter ceiling is inlined at `meter_reader.h:97` while the floor and clipping
  threshold are named functions. `i_live_input.h:22-26` describes a dry-tap ring buffer in the
  present tense before it exists. `multi_tone_rack.cpp` failure paths can leave a plugin in the rack
  tree but absent from `branch.chain` (error-path only, PLAUSIBLE whether reachable).

### The 3D highway and the game

- **A 2-whole-step bend draws below the highway floor.** `highwayBendLiftY`
  (`highway_metrics.h:340-343`) is unbounded; 2D clamps the same quantity to three steps of
  two-thirds tail height (`tab_paint_core.cpp:700-704`). At shipped metrics a 4-semitone bend on
  displayed lane 4 puts the head at y = -0.10 — under the floor that the capo pass explicitly
  refuses to cross — and on lane 3 it pokes past the top of the fret grid, which is precisely what
  `highwayBendInverted` exists to prevent. With inverted string order (both products' default) those
  are the D and G strings, so this is standard rock vocabulary. Needs one shared drawn-bend-height
  authority that saturates against available headroom; ties into the parked
  `docs/plans/todo/2d-bend-waypoint-redesign.md`.
- **Chord grouping and repeat-box classification run every frame, with an unbounded backward walk.**
  `highway_renderer.cpp:2075-2288` rebuilds and sorts chord groups per frame, then walks the note
  stream backward per group with no lower bound (`:2195-2220`) — a 200-note solo before a muted chug
  is 200 notes revisited every frame for as long as the chug is visible — plus covering-shape
  searches from index 0 (`:2230-2239`, `:2480-2484`). None of it depends on the frame. Deriving it
  once in `makeHighwayViewState` deletes ~210 lines from the deadline path and moves the repeat rules
  into headless core, where they would finally be testable; today they have no tests because they
  live in the GPU path.
- **The windowing authority the project owns is applied to notes only.** `highwayVisibleNoteRange`
  is used at `highway_renderer.cpp:2008` and `:4763`, but beats, fret-hand positions, tap onsets, and
  shapes are scanned full-song every frame (`:1892`, `:2926`, `:1394`, `:1611`, `:2977`, `:1483`,
  `:1794`, `:1939`, `:2451`, `:4354`, `:4558`). Worst is `windowSampleTimes` (`:491-532`), called per
  shape rail and per window-following tail, each call allocating and walking every placement in the
  song then sorting and uniquing. `lower_bound`/`upper_bound` over the ramp interval, the shape
  `highway_window.cpp:41` already uses.
- **The box painter's sort is not a total order, so boxes flicker.**
  `highway_renderer.cpp:2520-2522` sorts by `start_seconds` alone with a non-stable sort; the note
  sweep 500 lines above (`:2033-2073`) uses a three-key total order and its comment explains exactly
  this failure. A tap-and-strum onset emits two boxes with equal keys and overlapping panels, which
  swap composite order between frames. One word: `stable_sort`.
- **The "hold a sustainless strum for its covering span" rule is implemented twice, in two units** —
  `highwayDisplayHoldEnds` (`highway_view_state.h:764-811`, seconds) and `chartEffectiveSustains`
  (`grid_arithmetic.cpp:92-140`, beats). Each names the other as its twin, and
  `highway_view_state.h:776` records that *both* carried the same latest-starting-span defect and
  were fixed separately — the clearest possible evidence the shape is wrong. Also why
  `g_highway_onset_match_epsilon` had to shrink to 1e-9. `makeHighwayViewState` holds the chart and
  tempo map, so it can call the beats authority and resolve to seconds, deleting the seconds copy.
- **The "show at least N strings" padding rule is written twice** — `highway_projection.cpp:53-56`
  and `tab_lane_layout.cpp:17-25` + `:67`. Agree today; the day one gains a wrinkle the two surfaces
  anchor the shared string palette to different lanes. One `displayedStringCount()` in core.
- **Per-frame allocation in the render path**: the visible section name is copied and uppercased
  every frame (`:4542-4545`) though it is a pure chart function, and every drawer allocates fresh CPU
  batches each frame (~19 sites listed in the review) while `Impl` holds no scratch members —
  roughly two dozen allocations per frame, plus `makeHighwayTailSampleTimes` allocating and sorting
  up to ~270 doubles per modulated tail.
- **The song-select menu has no viewport.** `game/ui/src/game/game.cpp:117-131`, `:158-162`,
  `:164-178` draw one row per library entry from a fixed origin. At 100 songs on 1080p, rows past 64
  are off-screen, the key-hint footer never appears, and selecting song 80 puts the highlight bar at
  y = 1328 — the player navigates blind. Also one heap `std::format` per entry per frame. Windowing
  the rows adds code, but a fixed-height screen genuinely cannot state an unbounded list.
- **`ScoringRuleset`'s version and constants must agree by hand** (`scoring_ruleset.h:25-113`): the
  doc says any constant change bumps the version, nothing enforces it, and every score record is
  stamped with that version and claimed self-describing. A factory keyed by a version enum makes the
  lie unrepresentable. Separately `timing_window.cpp:53-57` divides by a bare `double speed_factor`
  with no stated domain — at zero the hit window collapses and the recorded delta becomes NaN.
- PLAUSIBLE: `slide_state_at` (`:2718`) returns no slide state when `note.fret <= 0`, so an
  open-string note with waypoints would draw a straight tail while its waypoint furniture still
  draws at the waypoint frets — unconfirmed whether the rules refuse open-string slides. Also every
  dev-session hot reload calls `camera.reset()` (`:1251-1259`), so each save snaps the camera.

### Editor UI

- **The ruler's play-from-here flag goes stale on every horizontal scroll.** `TimelineRuler`
  pre-maps seconds to a local x (`timeline_ruler.cpp:176-200`), and the memo key that gates the
  re-push (`track_viewport.h:526-542`) omits the scroll position. Scroll while paused and the flag
  stays pinned to a screen pixel, pointing at the wrong musical time and visibly disagreeing with the
  paused column in the content. Fix deletes code: store the mark in seconds and map it at paint time,
  as the shape chips and the time selection already do — which removes the staleness class rather
  than adding a key field.
- **The paused-column visibility rule is stated three times with two different formulas**
  (`track_viewport.cpp:568`, `:587`, `:984`) and its inputs are missing from the memo key, so a lane
  resize on a chartless arrangement re-enables an overlay line that paints across the caret.
- **Three controls that lie about what they will do**: the audio-settings OK button ORs its own
  condition onto the controller's availability and then routes to a *different* intent
  (`audio_device_settings_view.cpp:533`); double-clicking a plugin row bypasses the Add guard the
  button honors (`plugin_browser_window.cpp:339-345`); and an insert slot renders an enabled-looking
  "+" that `hitTest` refuses (`insert_slot_view.cpp:49-50`).
- **Escape performs an affirmative action.** In the audio-device failure overlay
  (`audio_device_failure_overlay.cpp:155-158`) Escape *opens* the settings window, against the
  editor's cancel/dismiss ladder. This prompt has no cancel outcome, so Escape should be swallowed.
- **A one-way disable leaves the calibration window dead.**
  `input_calibration_window.cpp:249-261` disables three controls when read-only and has no branch
  that re-enables them, so unticking "use game audio settings" clears the tooltips and leaves the
  controls dead until an unrelated state push. Folding read-only into the view state makes `setState`
  the single authority and deletes the parallel path.
- **Any state push kills an in-flight tone-boundary drag** (`tone_track_view.cpp:60-76`): playback
  crossing a region boundary republishes the state and the drag silently snaps back. The automation
  lanes already solved this; the tone track still carries the flawed shape.
- **A view owns a model rule the controller also owns**: `advanceActiveRegion()`
  (`tone_track_view.cpp:717-743`) runs its own containment test, keys the result by *display index*
  against a codebase law that says identity is by value, fires a payload-less intent, and the handler
  recomputes the same containment. No `playing` gate either, so a paused seek fires an activation.
- **Six pushes that should repaint nothing repaint everything**, against the guide's stated
  equality-gate invariant: `signal_chain_view.cpp:346-362` has no gate at all and *destroys and
  recreates every plugin tile and insert slot on every push* (which is also what silently discards
  an open tile popup); `arrangement_view.cpp:167-179` re-draws the waveform — the most expensive
  draw in the editor — per caret step; `tone_automation_lanes_view.cpp:319-323`;
  `transport_controls.cpp:57-67` deep-copies an SVG `Drawable` per push, in the very file the
  developer guide names as the clean exemplar. All four have a defaulted `operator==` already
  available. Plus two glyph layouts in hot paths: shape-chip text measured inside `paint()`
  (`timeline_ruler.cpp:766-795`, breaking the rule the rest of that class documents at length) and a
  layout per lane per mouse-move (`tone_automation_lanes_view.cpp:426-434`).
- **~200 lines of dead wiring**: `onToneRegionResizeRequested` is emitted by nothing yet threads
  through six files including an editor-core action and its availability arms;
  `ArrangementView::Listener` can never fire because the cursor overlay never asks the waveform row
  for a hit; `MainWindow::ExitCallback` is stored, documented, and never invoked, with
  `main.cpp:168`/`:171` passing the same quit function twice.
- **`sameCaretMask` is 30 hand-rolled lines built on a premise the tree disproves.**
  `timeline_cursor.cpp:109-124` says `juce::Range`'s own `operator==` would trip `-Wfloat-equal`, but
  `track_viewport.cpp:107` compares `std::optional<juce::Range<float>>` with plain `==` and has been
  green through CI since 2026-07-19. Either the premise is wrong and ~35 lines delete, or it is right
  and that line is a latent three-platform break MSVC cannot see. **Resolve this one deliberately** —
  it sits exactly on a known CI blind spot, and the likely answer is that the comparison lives inside
  a JUCE header compiled as external, which suppresses the warning.
- **Four theme colors snapshotted at static-initialization time** (`tone_track_view.cpp:23`, `:26`,
  `tone_automation_lanes_view.cpp:51`, `insert_slot_view.cpp:21`) would keep the old accent forever
  across a theme swap, defeating the seam's stated purpose. Read the theme at paint time instead.
- **~30 color literals outside the theme seam** (full census in the review), and the theme has **no
  font or size roles at all**, so every font height in `ui/` is a literal by construction — that one
  is a decision, not a sweep.
- Smaller: `busy_overlay` and `audio_device_failure_overlay` each compute their centered geometry
  twice and are near-duplicates of one another with a comment admitting the hand-maintained
  agreement; `signal_chain_view::paint()` re-walks `resized()`'s layout arithmetic; the undo overflow
  hint draws into a leftover sliver so "+ N older" is clipped
  (`undo_history_overlay.cpp:83-90`); a keymap write failure is swallowed into an empty `if`
  (`editor_keymap_persistence.cpp:73-78`); `onPluginBrowserBusyCancelRequested` is a pure rename of
  an existing intent. PLAUSIBLE: `keymap_editor_view.cpp:349-373` removes a key press before removing
  the stored indices, so rebinding Redo to a chord it already owns may keep the chord it was asked to
  replace — needs a read of JUCE's index-removal path.

### One rule in two places, across the tree

- **Five `makeIdentity` test builders for `InputDeviceIdentity` that already disagree** — two derive
  the channel name from the index, three pin it to `"Input 1"`, and `samePhysicalInputRoute` ignores
  the field, so one set of tests passes for the wrong reason. Sites: `test_audio_config_store.cpp:109`,
  `test_input_calibration_workflow.cpp:16`, `test_live_input_monitor.cpp:36`,
  `test_input_calibration_projection.cpp:21` (a byte-for-byte copy across a library boundary),
  `test_gameplay_session.cpp:60`.
- **Sample rate formatted two ways for the same datum** — `"44100 Hz"`
  (`audio_device_settings_controller.cpp:40`) versus `"44kHz"` (`audio_device_status_text.cpp:39`),
  same feature folder, plus three restatements of one 0.001 tolerance in two different units.
- **Settings-file location policy stated four times, and one copy diverges** —
  `audio_config_store.cpp:40-54`, `editor_settings.cpp:37-53`, `game_settings.cpp:137-147`,
  `editor/app/main.cpp:53-60`. The game copy omits `millisecondsBeforeSaving`, which JUCE defaults to
  3000 where the others set 0, so it silently runs a 3-second auto-save timer the others do not. The
  `main.cpp` copy hand-rebuilds four path fields purely to find the file the store writes.
- **Shader and texture resource names hand-enumerated in five places, three unchecked**
  (`highway_renderer.h:43-68`, `highway_renderer.cpp:1134-1157`, `game_resources.cpp:56-84`,
  `highway_shader_loader.cpp:39-76`, `preview_resources.cpp:67-73` with raw strings and no enum). An
  eighth program needs five edits and the editor path would compile and render with a
  default-constructed program.
- **Plugin-identity JSON: 11 keys written twice** (`tone_document.cpp:129-139` writer,
  `plugin_scan.cpp:144-156` reader) and the two halves are one round trip, so renaming a key on one
  side silently drops the field with no error. **`InputDeviceIdentity` persistence keys likewise
  declared twice** with identical names and values (`audio_config_store.cpp:28-31`,
  `game_settings.cpp:32-35`) — an on-disk contract.
- **The terminal grid position is derived by hand four times** (`rock_song_package_read.cpp:544`,
  `tone_track_normalize.cpp:26`, `tone_track_rules.cpp:14`, `editor_controller.cpp:2439`). `TempoMap`
  owns both halves and should expose `terminalGridPosition()`.
- **The `Part` enum/string mapping is written four times with three different fallbacks and three
  naming schemes** (`arrangement.cpp:6-22` and `:24-41`, `editor_controller.cpp:3893-3912`,
  `gp_chart_builder.cpp:421-433`) — and it is the *persisted* token, so two display copies duplicate
  persistence vocabulary.
- **ASCII-lowercase fold written six times**, with `hasFlacExtension` and `hasVst3Extension`
  differing only in the compared literal. One `hasExtensionIgnoringCase` in `common/core/shared/`.
- **The product resource-deploy rule is ~35 duplicated CMake lines** in each app, already divergent,
  both hard-coding the shared texture path under different variable names — moving that directory
  breaks both deploys with no configure error, because the glob just matches nothing.
- **An unnamed `1.0e-9` beside a named one** for the same "same onset" test
  (`highway_tail.cpp:225` versus `g_highway_onset_match_epsilon`), plus four bare `1e-9` in
  `gp_chart_builder.cpp`. `highway_renderer.cpp:212` aliases the named constant correctly — the
  pattern the others should follow.
- Smaller: a fourth namespace level at `project_io.h:17` against the Two-Axis Rule, where every
  sibling in the folder is flat; a missing `<span>` include at `engine_tone_automation.cpp:216`;
  three spellings for a `.cpp` including its own header (192 / 10 / 1); duplicated test helpers
  (`getPlayPauseButton`, `getStopButton`, and three copies of `getIndexOfDevice`); `tab_view.h:30-97`
  restating four shared paint-core rules in prose whose implementations are one-line delegates; a
  `get` prefix on a pure derivation at `project_handlers.cpp:75`.
- Structural: `TempoMap` has no validator of its own, so its rule set lives only in the *package*
  feature while the header documents its constructor as taking "already validated" input with nothing
  to call. Not yet a duplication, but it is the shape that becomes one.

### Documentation claims that are wrong

Twelve verified-wrong header claims. Four were fixed with their code; these are the rest, each
stating a rule the code does not implement:

- `highway_view_state.h:575` documents the pre-fix rule (fret-zero notes skipped) that a deliberate
  fix replaced with the sounding fret; the inline comment 12 lines above says the opposite.
- `tab_view_state.h:57-62` documents a `linked` discriminant the chart rules make impossible; the
  real rule (offset versus sustain end) is stated correctly in `tab_projection.cpp:63-68`.
- `chart_rules.h:275` claims `validateChartRules` requires positive sustains; zero is legal and is
  the normal encoding. Its rule list at `:273-282` also reads as exhaustive while omitting most of
  what now runs, and `:171-172` puts the hammer-landing rule in the relational half when it is
  enforced in the one-note half.
- `highway_view_state.h:433` calls `string_count` the count the chart uses when it is the padded
  displayed count, so a consumer indexing the tuning with it reads out of range; `:455` names only
  taps when the producer keys on taps *or* pick slides; `:242` versus `:294` document one
  quantization for two (one `ceil`'d, one raw).
- `tab_view_state.h:53` and `highway_view_state.h:89` carry the same wrong brief on both surfaces —
  `unpitched` means "this waypoint is unpitched travel", not "the glide trails off", and every
  waypoint of a scrape is flagged.
- `highway_window.h:76-77` places the coverage ramp a full lane too far in; the `.cpp` is exact.
- `tab_paint_core.h:50` states a fret-hand-harmonic-only scope that contradicts both the code and its
  own next paragraph.
- Three `\ref`s to symbols that do not exist (`highway_tail.h:60`, `:236`, `:264`), all naming a
  superseded world-space design the same header explicitly rejects — these will emit unresolved-`\ref`
  warnings in the CI Doxygen job. Plus `highway_projection.h:29` omitting the one option field with
  real consequence, and `chart.h:418` citing a rule id that no longer has a row.
- 11 missing `\param`/`\return` fields (listed in the review) and 33 over-100-column comment lines.
  **But `documentation-conventions.md` contradicts itself first**: its block-format section offers a
  brief-only one-liner on a non-void function as canonical while its required-fields section forbids
  exactly that. Decide which rule wins before sweeping; practice overwhelmingly follows
  required-fields.
- `makeTabLaneMetrics` (`tab_paint_core.h:122-124`) and `paintTabLane` (`:162`) state preconditions
  that nothing asserts or guards, and `GridPosition::offset`'s `[0, 1)` range is enforced only in
  another module while several `grid_arithmetic.h` functions silently depend on it.
