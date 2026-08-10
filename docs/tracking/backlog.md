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
- **The ZIP-safety check narrows package-supplied text** with the conversion this codebase elsewhere
  documents as throwing on MSVC outside the active code page, from inside a typed-failure API. Five
  error-message constructions do the same while building a failure report.
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
