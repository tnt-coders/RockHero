# Plan 64 — Album-Scale Performance

## 1. Status

**Authored 2026-10-03** against `master @ 51afa283` plus the working tree's Step B
(`HeadMargins`, `makeHighwayViewState(scene, …)`), from the read-only whole-app analysis of the
same day (relwithdebinfo, tiled corpus at 3k / 10k / 30k / 50k notes; raw logs in the session
scratchpad, never in the repository). Findings 1, 3 and 6 (§6) are Phase 1, in progress: each goes
through a pre-implementation review pipeline before it is built (§11 Phase 1). Phase 0
(measurement) gates the shape of Phases 2, 4, 5, 6 and 7 and may run in parallel with Phase 1.
Phases 0 and 1 run now; Phase 2 onward is scheduled by the user. Nothing else has started.

The user's question — "did you determine that every problem is actually fixable?" — is answered
in §7: every finding has a structural fix whose complexity class is proportional to the change;
two of them (the windowed re-derivation's mechanism and the engine resync's framework floor) carry
an unknown that Phase 0 settles, and each has a named fallback that is still structural.

## 2. Goal

A charter opens a full-album chart — about 90 minutes, several arrangements of 20k–50k notes
each, hundreds of tempo anchors, thousands of keyframes and hand markers — and every interaction
feels exactly as it does on a four-minute song: a chip drag or held nudge tracks the pointer at
frame rate, a caret step or an edit or an undo is instantaneous, and the load, save and import are
bounded waits behind the busy overlay. The cost of every path becomes **proportional to the work
asked for** (the window a revision touched, the viewport a frame shows, the notes a plan moved),
never to the song, and every change is pinned bit-identical to today's derived state.

## 3. Non-goals

- No change to WHAT is drawn, scored or saved. Every phase is pinned bit-identical (§4) against
  the from-scratch derivation; a changed drawn result is a defect in the phase, never a new
  expected value.
- No timers, event coalescing, debouncing or frame skipping that hide a cost instead of removing
  it; no caches keyed on fewer than all of their inputs; no work skipped behind a policy flag
  — in particular no rebuild skipped by consumer presence (the highway only while the preview is
  open), no memo of the head margin (`minimumSustainDistance`, `marginBefore` before
  `987b62cf`) keyed on the map or on spans, and no lazy re-timing of a scene
  whose drawn laws are stated in seconds (the rulings of the 2026-10-02 projection-cost design,
  which stand here).
- No change to the audio thread or the playback clock. Nothing in this plan touches the audio
  callback; its rules (architecture.md "Rules") are unchanged and stay the first correctness bar.
- No format change. The streaming reader (Phase 3) reads the document the writer emits today.
- No hand-rolled JSON tokenizer, segment tree or interval tree: the standard library first, then a
  Conan library the user approves (CLAUDE.md "Existing Libraries over Hand-Rolled Algorithms").
- Not the FHP derivation's own cost (plan 60 owns the tracker and measures it on its census), and
  not the tone designer, plugin windows or device settings.

## 4. Constraints

- (a) **Budgets are correctness on the deadline paths.** The per-frame render path has 16.7 ms at
  60 Hz of which JUCE's message pump and the OS take their share (architecture.md "Timing and
  Latency": "Render frame delay: up to 16 ms at 60fps"; "UI thread (main thread): … In the
  editor, JUCE owns this loop"). The audio thread takes no locks, no allocation, no blocking
  (architecture.md "Rules").
- (b) **Measure on `relwithdebinfo` only**, through `.agents/rockhero-build.ps1`, with the
  relevant `*_tests.exe` deleted before any run whose number decides a design (CLAUDE.md "Runtime
  Performance", "Agent-Run Builds"). Every figure in this plan carries its input size. Three or
  more sizes spanning an order of magnitude per path; medians, never single runs.
- (c) **Bit-identity is the pin.** Every phase proves it the same way: the existing suites pass
  unedited; a hash of the full derived state (`ChartViewState`, `HighwayViewState`, every double
  through `std::bit_cast<std::uint64_t>`) over the local corpus is identical before and after;
  and where the phase introduces a second way to compute something (incremental vs from-scratch,
  new parser vs old), a property test asserts equality of the two after every step.
- (d) **Layering.** Derivation passes stay in `rock-hero-common/core` (`chart/`, `highway/`); the
  editor's controller and `Session` carry the dirty range; Tracktion stays behind
  `rock-hero-common/audio` (architectural-principles.md "Placement Procedure for New Files").
- (e) **Corpus content never enters git, a report or CI.** Probes and benchmarks read paths from
  environment variables, follow the `test_local_*.cpp` / `[.local-corpus]` hidden-tag pattern
  (`rock-hero-editor/core/tests/CMakeLists.txt:80-85`), and tile the corpus in memory.
- (f) **Simplicity yields only to correctness.** Every fix below deletes a rebuild before it caches
  one; where structure is added (the dirty range, the checkpoints) §7 names the design underneath
  that would have made it unnecessary and Phase 0 tests whether that design is available.

## 5. The target and its budgets

Scale target (the user's, 2026-10-03): ≈90-minute songs; 20k–50k notes **per arrangement**;
several (plan for 4) arrangements; hundreds of tempo anchors (plan for 800; the tiled corpus
reached 1,753 with no measurable effect); thousands of keyframes and hand markers (plan for
3,000 keyframed notes); thousands of tone automation points.

| Interaction class | Trigger | Budget | Source |
|---|---|---|---|
| **Pointer event / drag / held key repeat** (tempo chip drag, `Alt+←/→` run, selection drag, hover) | per pointer-move or key repeat, inside one paint | **≤ 8 ms** for the model commit + re-derivation, so the whole frame including JUCE paint stays **≤ 16.7 ms** | architecture.md "Timing and Latency" (16 ms frame); RAIL's "about 10 ms" for the app's share |
| **Caret / key / edit / undo / redo** (arrow key, digit, sustain, legato, Delete, `Ctrl+Z`) | per keystroke | **≤ 50 ms target, 100 ms hard** | Nielsen's 0.1 s "feels instantaneous"; a 30 Hz key repeat needs the target |
| **Whole-song verbs** (plan 66's Scale Tempo and Note Lock commit, plan 65's meter edit, paste over the song) | per command | **≤ 250 ms**, one undo entry; never driven per pointer event (§11 Phase 7) | explicitly O(song) work, inside Nielsen's 1 s "flow" bound; user signs (64-Q4) |
| **Load / save** | per open / `Ctrl+S` | open **≤ 2 s** to an interactive editor for 4 × 50k, busy overlay beyond 1 s; save **≤ 1 s** | Nielsen's 1 s / 10 s; user signs (64-Q4) |
| **GP import** | once per import | **≤ 10 s** with progress for a 50k-note score, behind the busy overlay | Nielsen's 10 s with feedback; user signs (64-Q4) |
| **3D preview / game frame** | per frame | **≤ 16.7 ms**, CPU-side view queries ≤ 1 ms, no allocation on the audio thread | architecture.md "Timing and Latency", "Rules" |
| **Audio callback** | per block | unchanged — untouched by this plan | architecture.md "Rules" |

## 6. Current state (verified 2026-10-03, relwithdebinfo, i7-12700K, tiled corpus)

Track A tiled 1× / 3× / 10× / 16× = 3,172 / 9,516 / 31,720 / 50,752 notes (anchors 51 → 801,
4.6 → 73 min). Medians; the full tables are in the analysis.

| ms | 3,172 | 9,516 | 31,720 | 50,752 | Budget | Verdict |
|---|---|---|---|---|---|---|
| **EVENT tempo-chip drag** (one pointer event) | 9.1 | 27.1 | 93.8 | **154.9** | 8 (16.7 frame) | 9× over; whole scene + board rebuilt per revision |
| **EVENT caret step** (settle, no commit) | 0.93 | 6.6 | 76.0 | **202.6** | 50 / 100 | superlinear: O(K × n) caret scan |
| EVENT type digit (plan only) | 1.96 | 6.66 | 24.0 | 37.6 | 50 | within budget but O(n log n) per keystroke, paid again at every gesture step |
| EVENT settle typed digit (commit + rebuild) | 11.4 | 34.7 | 120 | **207** | 50 / 100 | 2× over the hard budget |
| EVENT undo / redo | 8.7 | 25.5 | 88.7 | **151 / 174** | 50 / 100 | over; the rebuild, not the history |
| `makeChartViewState` (whole scene) | 7.7 | 23.5 | 81.8 | 130.6 | — | 2.57 µs/note, linear; `deriveChartShapes` 56 % |
| `makeHighwayViewState` incl. scene copy | 0.75 | 1.98 | 6.46 | 17.0 | — | linear |
| `parseChartDocument` (one arrangement) | 32 | 100 | 323 | **571** | ≈ 500 per arrangement for a 2 s open | 11.2 µs/note through a `juce::var` tree |
| `chartDocumentText` (save) | 2.0 | 5.0 | 18.6 | 29.6 | — | fine |
| per-frame board queries (camera, hand window, visible range), µs | 0.36 | 0.55 | 0.80 | 0.85 | 1,000 | log n, fine |
| View push, no revision | 0.008 | 0.010 | 0.015 | 0.017 | 8 | fine |

Phase 1 so far, at 50k (track A, same machine; event figures drift ≈ 11 % between runs, so each
is the same run's before/after): fixes 1 and 6c took the caret step from 168 to 0.52 ms. 6a/6b's
one publication took the tempo-chip drag event from 137.7 to 136.4 ms and the settle-typed-digit
event from 177.8 to 175.5 ms, both inside run drift; its real saving is on the other clock — the
preview's per-revision board copy, 5.37 ms on the frame that receives it, is gone. The two are
never summed. Fix 3 (the gate judges only the touched notes) took "type digit, plan only" from
39.4 to 2.62 ms (15×, now O(n) at ≈ 52 ns/note), missing Phase 1's ≤ 2 ms exit by 0.6 ms; the
remainder is the planner's whole-stream copy (1.24 ms) plus two diff walks (≈ 0.5 ms each), which
only delta composition removes, so it is assigned to Phase 2 — 2.6 % of the 100 ms budget per
keystroke. What remains in every event is the ≈ 130 ms whole scene rebuild, F2.

The whole-song verbs built on 2026-10-04 (plans 65 and 66) measure, at ≈ 50k notes:
- a meter edit costs ≈ 98 ms (validation 29 ms);
- Scale Tempo costs 147–231 ms;
- a Note Lock tempo edit costs ≈ 272–345 ms, after the shared nearest-tick seek
  was seeded from the beat position (snap-off placement went from 0.78 to 0.16 µs per call).

About 115 ms of each is the scene re-projection after the commit (F2). So F2's windowing is also what brings these verbs under the proposed 250 ms bound (64-Q4). One more lever belongs to the
same phase: a Note Lock φ is exactly the identity before the first anchor the edit
changes (and after the last, for a move or delete), so the re-address can skip those slices —
the same "derive only the changed range" decision 1 states for the projections.

Track B (2,164 → 25,968 notes, 147 → 1,753 anchors, 48 → 576 keyframed notes) agrees and shows
the caret step's second variable: 2.4 → 21 → 144 → **328 ms**. Anchors moved nothing measurable
(every tempo lookup is a binary search). GP import census: 25.3 µs/note end to end over 113
files, with the build's per-note cost rising from 6–7 µs at 300 notes to 16–33 µs at 6k–10k.

Key sites: `refreshChartProjections`
(`rock-hero-editor/core/src/controller/editor_controller.cpp:1960`) keyed on
`Session::chartRevision()` alone; the scene passes `chart_legato.cpp`, `chart_shapes.cpp`,
`chart_presentation.cpp`, `chart_projection.cpp`; the board `highway_projection.cpp`; the plan
path `chart_edits.cpp` (`finalizePlan`); the settle path `chart_handlers.cpp`
(`dissolveSilentKeyframes` → `chartNotesInFocus` → `chartPathTailAt`); the reader
`chart_document.cpp:599-730`; the engine resync `tempo_handlers.cpp` → `tone_automation_rebuild.cpp`
→ `tone_automation_curve.cpp`, `tempo_mirror.cpp`.

## 7. Feasibility — the honest answer

Every finding has a structural fix whose cost is proportional to the change. What differs is how
much of each fix is proven today.

| # | Finding | Fix class | Status of the fix | What could make it infeasible | How it is settled | Fallback if infeasible |
|---|---|---|---|---|---|---|
| F1 | Caret step O(K × n) | O(K + log n) | **PROVEN by reasoning**, in review (Phase 1): the stream is sorted and the same-string clamp re-applied by every `finalizePlan` guarantees at most one same-string ring covers an instant, so "first forward match" is "last same-string note strictly before, if its ring covers" (`lower_bound`, then walk back); the focus slots are computed once per settle | Nothing: the invariant is the one `normalizeSustainOverlaps` enforces | Phase 1's census probe asserts old == new `chartPathTailAt` at every onset and mid-ring instant over the corpus | — |
| F3 | Plan is O(n log n) per keystroke | O(k log n + k · neighbours) | **PROVEN by reasoning**, in review: `sustainBoundOf` is already neighbour-local and the live chart is already a fixpoint of the gate, so the gate's own authorities run over the affected set only | Nothing | identical `ChartEditPlan`s both ways over a generated edit set | — |
| F6 | Whole-value copies per revision and settle | removes ≈ 15 ms/edit at 50k | **PROVEN**, in review: one published board with the scene aliased into it, the preview holds the published pointer, the legato sweep copies only once it finds work | Nothing | scene/board hash identical | — |
| F2 | Scene + board rebuilt whole per revision — **the finding that decides album scale** | O(window + checkpoints) per revision, plus an O(n) memmove over doubles ≈ 0.1 ms at 50k | **MODEL PROVEN, MECHANISM PLAUSIBLE.** Proven: a moved anchor changes seconds only inside its two adjacent spans (every other anchor pins its time), so every seconds-stated law changes only for heads whose margin or ring crosses those spans; the shapes walk is a beat-domain state machine whose one tempo input is the landing-span quantum (`chart_shapes.cpp:523`); a chart edit's range is its removed/inserted onsets extended by their rings. Unproven: that the walk's state **converges** after a window (checkpoint equality) or is **dead at a silence cut** (segmentation) | (i) The walk's hand state carries information arbitrarily far (fingers keep their last stop after the ring ends; `covers_at` reads them regardless of `sounds`), so a window never converges and the incremental path degrades to a full rebuild — correct but no faster. (ii) Silence cuts are too rare in let-ring-heavy music to bound a segment | Phase 0-f: the silence-cut property test over the corpus and the segment-length distribution; Phase 2a's state inventory of the walk | If neither converges nor cuts: restructure the walk so its state is local by construction (the fingers' tenure ends with the ring — the design underneath, §7 below); this is the larger refactor of `chart_shapes.cpp` but still bounded and pinned by the same property test. The hard floor is the memmove, far under budget |
| F4 | Load parses 11 µs/note | ≤ 1 µs/note predicted | **PROVEN in principle** (the writer emits one flat object per note; streaming parsers read such documents at ≥ 1 GB/s); needs the library decision (64-Q1) | Licence or Conan availability of the chosen library; MSVC/GCC/Clang support | Phase 3's `parse_old == parse_new` pin over every corpus document and the malformed-document cases | Second candidate on the list; the writer is already fine and `juce::JSON` stays for `song.json` |
| F5 | `deriveChartShapes` constant 1.43 µs/note | 2–3× on the pass predicted | **PLAUSIBLE**; the specific allocations are read (`chart_shapes.cpp:665-709`, `:369`, `:531-537`) but not attributed | A profile showing the cost is in `Fraction` arithmetic instead, which the cost design's §3 keeps off the table until proven | Phase 0-e: VS CPU-Usage sample at 50k | Not needed for the budgets once F2 lands (the window is small); it only lowers the window constant and the whole-song verbs' cost |
| F7 | GP import grows superlinearly | O(n) passes | **PLAUSIBLE**: the trend is measured, the pass is not identified | Nothing structural: every pass reads as O(n) with per-voice maps; one of them is not | Phase 0-c: tiled `GpScore` at 3k / 10k / 30k / 50k | Behind the busy overlay once per import; a 5–10 s import is a wait, not a defect, if the budget holds |
| F8 | Engine resync per tempo edit is O(P + a) ValueTree ops | O(points in the two changed spans) | **UNMEASURED** (the editor harness uses fakes) | **A framework floor**: Tracktion's `AutomationCurve::addPoint` / `TempoSequence::insertTempo` each fire ValueTree listeners; if the per-point cost is ≈ 10 µs, 10k points cost 100 ms per drag event even when only the changed spans are rewritten | Phase 0-b: an audio-tests probe with a real `Edit` and a `VolumeAndPanPlugin` curve at 1k / 3k / 10k points, 300 / 1k / 3k anchors | Per-span rewrite first (proportional to the change). If the floor remains: the engine is a paused playback backend during tempo edits (plan 41: paused-only), so `syncTempoDerivedAudio` — which already keeps the map it last pushed and compares by value — pushes at **play start** instead of per commit. Its key is the whole map, so it cannot lie; it is lazy derivation, not a timer (user decides, 64-Q3) |
| F9 | Undo memory at album scale | bounded history | **ARITHMETIC, not yet done**: `ChartNote` is ≈ 100 B plus a `std::vector<Keyframe>`; a whole-song entry for 4 × 50k notes is tens of MB twice; the 100-entry cap (`editor_undo_history.h:353`) becomes GBs if every entry were whole-song | Nothing structural; only the cap's unit is wrong (entries, not bytes) | Phase 0-d: `sizeof` probe and the per-entry figure | A byte budget on the history (drop oldest past it), which is what the cap means |
| — | JUCE paint of the editor views at 50k | — | **UNMEASURED**; every paint and hit test reads viewport-bounded (`tab_paint_core.cpp:2690, 2782, 3146`; `chart_hit_testing.cpp:20-34`; `timeline_ruler.cpp:902` is O(anchors)) | An unbounded pass the reading missed | Phase 0-a: paint into a `juce::Image` at 50k, several zooms | Bound the pass by the viewport with `lower_bound` on the sorted timeline — the fix the lane already uses |
| — | bgfx / game frame at 50k | — | **CPU-side MEASURED fine** (0.85 µs per frame of board queries); GPU-side unmeasured; every draw pass reads viewport-bounded (`highway_renderer.cpp:2764, 2791, 5783, 5912, 6175, 6270`) | An unbounded buffer upload or pass the reading missed | Phase 0-a: Noop-device headless frame at 50k (CPU side) and a frame-time log in the running game on the tiled package | Bound by the visible range |

**The design underneath (constraint (f)).** F2 adds structure — a dirty range and either
checkpoints or segments. The shape that would have made it unnecessary is a scene that is a
**sequence of independent segments** cut where nothing sounds, each a pure function of its own
notes and the tempo spans it touches; Step B's "derive once per revision" is then "derive once per
segment per revision". Phase 0-f tests whether that shape is already available (the walk's state
is dead at a cut); if it is, segments replace checkpoints and the design shrinks. If it is not,
the finding is that the walk's hand state has a tenure the music does not — a modelling question
for plan 60 (the hand) as much as a performance one, recorded as such.

**So: yes, every problem is fixable**, and none of the fixes is a band-aid. Two things are not yet
known and are measured first: whether F2's incremental mechanism converges on real music (Phase
0-f, with a bounded fallback), and whether Tracktion's per-point cost is a floor (Phase 0-b, with a
lazy-by-value fallback the user signs).

## 8. Decisions

1. **The unit of derivation becomes the revision's dirty range, not the song — and the range is
   derived, never authored.** The projection cache keeps the notes, tempo map and sections its
   scene was built from and diffs them against the session's current ones (`diffNotes`' walk, the
   anchor diff); no writer states a range, so none can state a wrong one, and writes between two
   refreshes union for free. `Session` carries one revision counter per write kind (chart, tempo,
   sections) so a refresh skips the inputs that did not change — a tempo drag diffs only anchors;
   the single `chartRevision()` is deleted or derived from the three, never a fourth datum. Whole
   is just the covering range and is always correct, so the incremental path can only be faster,
   never different (Phase 2's property test is the proof). Reached in agreement between the
   simplicity and performance reviews (2026-10-04), replacing a range log on `Session`.
2. **Proportional, never coalesced.** No pointer event is dropped, delayed or merged; each is
   committed and derived in full. Where a verb is O(song) by its nature (Scale Tempo, the Note
   Lock, meter edits) it is a command, committed once per command or on release, and the plan
   says so where the user can see it (Phase 7). A tempo-chip drag is one such gesture in both
   lock states (66 decision 7, 2026-10-04): every event is published and drawn as a re-timed
   preview, and the write lands once, on release.
3. **One scene, one publication.** The controller publishes one `HighwayViewState`, and the tab
   scene is an aliasing `shared_ptr` into its `chart`, so the scene copy goes without making the
   scene nullable; the preview's renderer holds the published pointer. Both surfaces read one
   build (Phase 1).
4. **Libraries before hand-rolled parsers.** The streaming reader uses a Conan library the user
   picks (64-Q1); `juce::JSON` stays for the small `song.json`.
5. **The benchmark is part of the product's verification.** An env-gated, hidden-tag Catch2 test
   (`[.album-scale]`, the `test_corpus_census.cpp [.local-corpus]` precedent) tiles the local
   corpus to 3k / 10k / 30k / 50k and asserts every budget in §5 on relwithdebinfo. It is run
   before each phase's commit and at final acceptance; it never runs in CI.
6. **Behaviour preservation is pinned by hash and property, never promised.** Constraint (c).

## 9. Open questions

- **64-Q1 — the streaming JSON library** (gates Phase 3). Candidates, all to be re-verified on
  ConanCenter at phase start:
  - (A) **simdjson** — Apache-2.0 (GPLv3-compatible one way, so fine under AGPLv3); Conan
    `simdjson`; MSVC, GCC, Clang; the On-Demand API reads a document forward in one pass, which is
    exactly the writer's one-flat-object-per-note shape; the fastest of the four; C++17.
  - (B) **glaze** — MIT; Conan `glaze`; header-only C++23 with compile-time reflection straight
    into structs; our tokens (`"measure:beat+n/d"`, fractions) need custom readers anyway, so
    reflection buys little; heavier compile times; newest MSVC support.
  - (C) **RapidJSON** — MIT (its `jsonchecker` test data carries the JSON licence, not the
    library); Conan `rapidjson`; SAX API; mature, slower than A but far under 1 µs/note.
  - (D) **yyjson** — MIT; Conan `yyjson`; a C library whose immutable arena DOM avoids per-node
    allocation; fast; C API in a C++ codebase.
  **R: A (simdjson On-Demand)** — fastest, permissive, widely packaged, forward-only matches the
  document, and the project already carries larger Conan dependencies. C if the user prefers a
  SAX callback shape; D if a C dependency is acceptable and a DOM is wanted for error reporting.
- **64-Q2 — F2's mechanism: segments or checkpoints.** Settled by Phase 0-f, not by the user,
  unless the property fails AND the user wants the walk restructured (the fallback) rather than
  checkpointed. **R: segments if the property holds and the p95 segment is ≤ 2,000 notes (≈ 5 ms
  at today's 2.57 µs/note); checkpoints otherwise; restructure only if neither converges.**
- **64-Q3 — if Tracktion's per-point cost is a floor (Phase 0-b):** (A) accept per-span rewrite
  cost per drag event; (B) `syncTempoDerivedAudio` pushes at play start and rig load, keyed on the
  whole map by value (correct: the engine is paused during every tempo edit). **R: B if A exceeds
  2 ms per event at 10k points; A otherwise.** Only asked if Phase 0-b fires.
- **64-Q4 — the budgets the user signs:** the whole-song verb bound (250 ms), the open bound (2 s
  for 4 × 50k), the save bound (1 s), the import bound (10 s with progress). **R: as stated in §5.**
- **64-Q5 — the Note Lock ON during a chip drag — ANSWERED by plan 66** (decision 2, "a
  Note Lock drag commits on release"; `docs/plans/roadmap/66-tempo-repair-tools.md`).
  Recorded here with the correctness reason that stands before the performance one: re-addressing
  rounds every position to the tick lattice, so re-addressing per pointer event would compound
  rounding across the drag, while one re-address from the drag's starting map rounds once (the
  coalesced-run mechanism already computes "from the same starting state",
  `editor_undo_history.h:464`). Per-event re-address would also cost ≈ 150–250 ms per event at 50k.
- **64-Q6 — the undo history's unit:** (A) keep 100 entries; (B) a byte budget (recommend 512 MB,
  oldest dropped past it), with 100 entries as the count cap too. **R: B.**
- **64-Q7 — where the benchmark lives:** (A) a gitignored `test_local_album_scale.cpp` recreated
  per session; (B) a committed, env-gated `[.album-scale]` hidden-tag test beside the census.
  **R: B** — it survives sessions and the census is the precedent; it reads
  `ROCKHERO_ALBUM_SCALE_CORPUS` and skips without it.

## 10. Dependencies

- **Plan 41** (tempo map authoring): Phase 4's chip drag is the pointer-rate path this plan makes
  affordable; 41 Phase 2's `syncTempoDerivedAudio` is the authority Phase 6 here reshapes. 41's
  "after the projection-cost fix" is this plan.
- **Plan 65** (time-signature editing, split from 41 on 2026-10-03) and **plan 66** (tempo repair
  tools: Scale Tempo, Note Lock, tempo lock) own the whole-song re-addressing transform
  and its whole-song snapshot undo; both already cite this plan's figures and bound their phases
  on them. Phase 7 here budgets those verbs and sizes their snapshots; 66's decision 2 (a
  Note Lock drag commits on release) answers 64-Q5.
- **Plan 60** (hand markers): the FHP tracker is not in scope, but if Phase 0-f shows the shapes
  walk's hand state is not dead at a silence cut, that is a hand-model finding for plan 60.
- **Plan 40** (chart editing) owns the undo history whose cap Phase 7 re-units.
- **Plan 10** (format versioning): Phase 3 changes no byte of the format.
- **Plan 25 / 44** (highway, preview): Phase 1 moves the display options out of
  `HighwayViewState` into the renderer, which holds the published state by pointer; the game's
  dev session hands its board out as that pointer.

## 11. Phased implementation

Every phase: a build on relwithdebinfo, the touched tests, the corpus hash (constraint (c)), the
album-scale benchmark (decision 5) at four sizes with before/after medians recorded in this file's
status line, a simplicity review before the commit. Command forms are in `.agents/README.md`.

### Phase 0 — Measure the unmeasured (gating; parallel with Phase 1)

Each probe is a temporary `test_local_*.cpp` (constraint (e)) unless stated, run from the built
test executable by tag, deleted afterwards (`git status` clean). Results go to the session
scratchpad and, as figures, into this file's §6.

- **0-a JUCE paint and the game frame at 50k.** Editor: construct the tab view, ruler and marker
  rows against a 50k-note `ChartViewState` (the tiled corpus) and paint them into a 2560×1440
  `juce::Image` through `Component::paintEntireComponent` at three zooms (song, section, bar);
  50 paints each, median. The 3D preview: a Noop render-device frame with the 50k board (the
  `rock-hero-common/ui/tests` Noop suite is the harness) for the CPU side; for the GPU side, write
  the tiled package to a scratch path and run the game on it with its frame-time log. **Gates:**
  whether a viewport-bounding phase is needed anywhere (expected: no; every pass reads bounded).
  Pass: paint ≤ 8 ms, frame ≤ 16.7 ms at every zoom.
- **0-b Tracktion resync with a real rig.** Probe in `rock-hero-common/audio/tests/` over
  `TempoMirrorHarness` (`test_tempo_mirror.cpp:26`) — that target's source list is explicit, so
  a temporary one-line `CMakeLists.txt` edit adds the probe and is reverted afterwards (with
  `-Configure` once each way). Time `mirrorTempoMapIntoSequence` at 300 / 1,000 / 3,000 anchors
  and `writePluginParameterCurve` on a `VolumeAndPanPlugin` at 1k / 3k / 10k points, each as one
  tempo edit would call them; report per-point and per-anchor cost. **Gates:** Phase 6's shape and
  64-Q3. Pass: a full resync ≤ 2 ms at 10k points (then Phase 6 is unnecessary).
- **0-c GP import at 50k.** Tile a parsed `GpScore` (`gp_score.h`) by repeating its master bars
  with shifted indices, time `buildGpSong` (`gp_chart_builder.h:87`) at 3k / 10k / 30k / 50k, and
  time each builder pass separately (`letRingFigureEnds`, `resolveSlideOutExits`,
  `clampSameStringOverlaps`, the per-voice maps) to name the superlinear one. **Gates:** Phase 5's
  target pass, or its removal if the curve is linear and under 10 s.
- **0-d Undo memory.** Print `sizeof(ChartNote)`, `sizeof(Keyframe)`, the mean keyframes per
  note over the corpus, and the resulting whole-song memento size for 4 × 50k; the 100-entry
  worst case. **Gates:** Phase 7's byte budget (64-Q6).
- **0-e A sampling profile of `deriveChartShapes`.** Run the album-scale probe's shapes loop at
  50k under Visual Studio's CPU Usage tool (relwithdebinfo, symbols), steady state, three
  captures; report self time by function: per-slot vector allocation, `std::map<ChartPosture>`
  emits, `Fraction` gcd, `span_reach`, other. **Gates:** Phase 4's content, or its deletion.
- **0-f The silence-cut property.** For every chart in the corpus, find each silence cut (an onset
  where the prefix-max ring end ≤ the onset: nothing from before sounds). For each cut, run
  `deriveChartShapes` from scratch on the suffix starting at the cut and compare its spans and
  postures (postures compared by value, indices re-numbered by first appearance) with the full
  run's output after the cut. Report: charts where every cut is bit-identical; the first
  differing cut and the state field that differed; the distribution of segment lengths (median,
  p95, max, in notes) per chart. **Gates:** 64-Q2 — segments vs checkpoints vs restructure.
- **0-g The derived range's price.** At 3k / 10k / 30k / 50k, time `diffNotes` over the retained
  and current note streams after a one-note edit, the retained copy itself, and the anchor diff of
  two maps. Predicted 1–2 ms walk + 1.5 ms copy at 50k, µs for the anchors. **Gates:** decision 1;
  the range yields to an authored one only if this breaks a §5 budget. The retained copy
  (≈ 6 MB per 50k-note arrangement) joins Phase 7's byte budget.

**Exit:** every probe reported with preset, sizes, medians and spread; §6 and §7 updated; 64-Q2
answered; 64-Q3 asked only if 0-b fired. No production file changed; `git status` clean.

### Phase 1 — The proportional small fixes (F1, F3, F6) — IN PROGRESS

Each fix passes a pre-implementation review pipeline before it is built (user, 2026-10-03): an
Opus simplicity-expert evaluation, then the performance expert's final design. The simplicity
evaluation reshaped all three:

- **F1** `chartPathTailAt` takes `lower_bound(notes, position)` and walks back to the last
  same-string note (an `upper_bound` start stops on a head standing at the instant and loses the
  ring ending there, `at_ring_end`); `dissolveSilentKeyframes` reads a focus-slot list computed
  once per settle in place of the per-slot predicate. Deletes the K from-the-head scans.
- **F3** `finalizePlan` stays the one gate and runs its same per-note and pair authorities over
  the affected set in stream order (identical first refusal). Plans emitting deltas that compose
  is the deeper shape: it would also delete `base` and the settle fold's copy-and-diff. Agreed in
  the second review round (2026-10-04): the candidate's sort is deleted — planners insert through
  one `insertAtSlot` (at `lower_bound`, never refusing an occupied slot, so a duplicate lands
  beside its twin for the pair check's message), `planMoveSelection` sorts only its keyed notes
  and `inplace_merge`s, and `finalizePlan` asserts sortedness; the touched set is
  `diffNotes(chart.notes, candidate).inserted`, the existing walk, not a second one. The two
  writers that bypass the gate keep the live chart a fixpoint by lemma, each pinned by a test:
  the settle's Legato→Pick retype (no rule names `Legato`; validation verdicts are equal under
  the flip) and `dissolveSilentKeyframes` (the strip removes only what the path already states;
  every fixture note stripped still validates and normalizes to itself). Plan 65's meter edit is
  the one future writer that must re-establish the fixpoint.
- **F6** One published board with the tab scene an aliasing pointer into it; the renderer owns
  the display options and holds the published state. The legato sweep copies only after it finds
  work and walks per-string predecessors instead of building full connections; no dirty set.

The evaluation also found a latent bug: `Session::loadSong` never advances the chart revision
(`session.cpp:85-103`), so reopening the same project after an edit can keep a stale scene behind
the (arrangement id, revision) key. Fix: advance it in `loadSong`, pinned by a controller test that
edits, reloads the same package and compares against a fresh build.

**Testing:** `test_chart_keyframe_editing`, `test_chart_caret`, `test_chart_legato_editing`,
`test_chart_edits`, `test_chart_fret_entry`, `test_chart_sustain_gesture` pass unedited; the
census probe asserts old == new `chartPathTailAt` at every onset and mid-ring instant (each
queried with its own string), plus a unit case at an abutting head, and identical
`ChartEditPlan`s and refusals over a generated edit set; old and new legato sweeps give equal
streams over the corpus; scene/board hash identical.
**Exit:** at 50k — caret step ≤ 5 ms (from 203 / 328), type digit ≤ 2 ms (from 37.6), the
`ChartViewState` copy and the preview's board copy gone. **Verification:** build, touched tests,
hash, benchmark.

### Phase 2 — Windowed re-derivation (F2) — the album-scale phase

Sub-phases land in order, each shippable and pinned: a pass that is not yet windowed is derived
whole, which is always correct.

- **2a The derived dirty range and per-kind revisions** (decision 1). `Session` replaces
  `chartRevision()` with chart, tempo and sections revisions, each bumped by its mutating method.
  The projection cache retains the inputs of its last build and derives the range: a chart
  revision walks `diffNotes` over retained vs current notes (removed and inserted onsets extended
  by their rings); a tempo revision diffs the anchors (a changed anchor's previous to next anchor;
  a changed anchor count or terminal is the covering range); a sections revision is "board only"
  (zones derive from sections, the scene does not). The controller still rebuilds whole in 2a;
  this sub-phase only derives the datum. **Deliverable before any code:** the state
  inventory of the shapes walk — every field `deriveChartShapes` carries across onset slots
  (`hand[6]`, `open`, `covered`, posture table, pending spans), each classified local (dies with
  its ring) or carried (survives silence). 64-Q2's answer and this inventory fix 2b's mechanism.
- **2b The shapes walk, windowed.** Either (segments, if 0-f proved the property) the walk runs
  per segment and a revision re-derives the segments its range touches; or (checkpoints) the walk
  records its state every 64 onset slots with fingers as **slot keys**, restarts from the last
  checkpoint ≤ range.lo and stops at the first checkpoint ≥ range.hi whose state equals the stored
  one, running to the end when none converges (degrades to today, never wrong). Spans and postures
  are spliced; the posture table is re-numbered by first appearance in one O(spans) pass so the
  output equals from-scratch bit for bit.
- **2c Connections, presentation, holds, projection seconds, windowed.** Connections (per-string
  predecessor chain) re-derive for the window plus the first note after it on each string.
  Presentation re-derives for the notes in the window plus every note before it whose
  `inkEndOf` lookahead reaches into it (found through the prefix-max ring-end table). Holds and
  `SpanCover` re-derive for the spans overlapping the window. Projection seconds and
  `margin_start_seconds` re-derive for notes in the changed tempo spans plus heads whose margin
  window crosses them. Index-keyed fields (`end_head`, `note_group`, `display_hold_ends`) are
  remapped by the insert/delete offset; the prefix-max tables are recomputed from the splice point
  to the end — O(n) over doubles, ≈ 0.1 ms at 50k, accepted as the floor.
- **2d The board, windowed.** `makeHighwayViewState`: chord groups compare only with the previous
  group, lights are a merge sweep, beats and zones are per measure — re-derived over the same
  window with the same remap; zones alone on a sections-only revision.
- **2e The controller and the fallback.** `refreshChartProjections` derives the range from its
  retained inputs; a range → `updateChartViewState(previous, arrangement, tempo_map, range)` (new,
  beside `makeChartViewState` in common/core, the editor its consumer); the covering range →
  today's rebuild. The two paths are the two sides of the property test.

**Testing:** every existing scene and board suite unedited. New: a property test (CI-safe, on
synthetic charts with chords, legato chains, keyframes, a 7/8 bar, sub-beat anchors) and its
corpus twin (`[.local-corpus]`) that apply a recorded edit script — anchor moves and nudges,
inserts, deletes, retypes, sustain changes, undo and redo — and after **every** step assert
bit-identity of `ChartViewState` and `HighwayViewState` between the incremental build and
`makeChartViewState` / `makeHighwayViewState` from scratch; a convergence census (how often a
window converged, and where). **Exit:** at 50k — tempo-chip drag event ≤ 8 ms (from 155),
settle-with-commit, undo, redo ≤ 50 ms (from 150–210); zero property failures over the corpus.
**Verification:** build, touched tests, both property suites, hash, benchmark. **Size:** a
multi-week refactor across `chart_legato` / `chart_shapes` / `chart_presentation` /
`chart_projection`, `highway_projection`, `Session` and the controller (≈ 1.5–2.5k lines incl.
tests).

### Phase 3 — Streaming load (F4) — on 64-Q1

Add the approved library to `conanfile.txt`. `parseChartDocument` reads the writer's
one-flat-object-per-note document straight into `ChartNote` with no intermediate `juce::var`
tree; `juce::JSON` stays for `song.json`. The error vocabulary is unchanged (typed boundary
errors, architectural-principles.md). **Testing:** `parse_old(text) == parse_new(text)`
(defaulted `Chart::operator==`) over every corpus document in a `[.local-corpus]` test and over
the suite's fixtures and malformed-document cases unedited; the old parser is deleted in the same
change once the pin passes (never two readers in the tree). **Exit:** ≤ 60 ms per 50k-note
arrangement (from 571); a 4 × 50k open ≤ 2 s to interactive. **Verification:** build (with
`-Configure` for the Conan change), touched tests, benchmark.

### Phase 4 — `deriveChartShapes` constant factors (F5) — per Phase 0-e

Only what the profile names: `std::array<…, g_max_chart_strings>` for the per-slot tables (no
allocation per slot), postures hashed by value into a flat table, and nothing on `Fraction`
unless the profile attributes the cost to it (then it is its own decision — value equality and
map keys ride on eager normalization). **Testing:** `test_chart_shapes` exact-value cases
unedited; hash identical. **Exit:** the pass's µs/note halved, which lowers Phase 2's window
constant and the whole-song verbs' cost. **Deleted** if Phase 0-e shows no dominant allocation.

### Phase 5 — GP import (F7) — per Phase 0-c

Replace the pass Phase 0-c names with a forward cursor or merge over the sorted stream (per-voice
`std::map` lookups become a two-pointer walk). **Testing:** `test_gp_song_importer` unedited; the
import census's per-file `Song` hash identical over the corpus. **Exit:** import linear in notes
and ≤ 10 s at 50k with progress. **Deleted** if Phase 0-c measures linear and under budget.

### Phase 6 — Engine resync proportional to the change (F8) — per Phase 0-b

`rebuildToneAutomationCurves` rewrites only the points in the changed tempo spans (the only
points whose seconds moved) and the mirror re-inserts only the changed anchors; if Phase 0-b shows
the per-point floor still breaks 2 ms per event, 64-Q3 B: `syncTempoDerivedAudio` runs at play
start and rig load, keyed on the whole map by value. **Testing:** `test_tone_automation_rebuild`,
`test_tempo_mirror`, the controller's `last_mirrored_tempo_map` fixture unedited; a probe asserts
the rewritten curve equals a full rewrite point for point. **Exit:** resync ≤ 2 ms per event at
10k points, or not on the event path at all. **Deleted** if Phase 0-b measures under 2 ms.

### Phase 7 — Undo memory and the whole-song verbs (F9; plans 65 and 66)

- The history's cap becomes a byte budget (64-Q6) beside the entry count; whole-song snapshots
  report their size. Phase 0-d's figure is handed to plan 65 before its snapshot phase ships
  (65 asks for exactly that).
- The whole-song verbs — plan 66's Scale Tempo and Note Lock commits, plan 65's meter
  edit — derive the covering range, rebuild whole (≤ 250 ms at 50k after Phases 2 and 4), and
  are committed **once per command or on release** (66 decision 2): `Alt+←/→` with the Note Lock
  ON commits per press (an explicit command each); the chip drag with it ON re-addresses on
  release, one undo entry, computed from the drag's starting map so the tick rounding happens once
  (64-Q5). With the toggle OFF the drag also lands once on release (66 decision 7), so its settle
  is Phase 2's windowed path run once per gesture. This phase contributes the
  budget, the benchmark rows and the snapshot sizing; the verbs themselves are built in 65 and 66.
- **Testing:** `test_editor_undo_history` for the byte budget; the benchmark's whole-song rows.
  **Exit:** 100 entries at 4 × 50k stay under the budget; every whole-song verb ≤ 250 ms at 50k;
  no whole-song work on a pointer event.

### Phase 8 — Final acceptance: the album-scale benchmark

The env-gated `[.album-scale]` test (64-Q7) tiles the corpus to 3k / 10k / 30k / 50k and, on
relwithdebinfo with the test executable relinked, asserts per size: tempo-chip drag event ≤ 8 ms;
caret step, type digit, settle-with-commit, undo, redo ≤ 50 ms; view push ≤ 1 ms; parse ≤ 60 ms
per arrangement; `chartDocumentText` + validate ≤ 250 ms per arrangement (save ≤ 1 s for four);
whole-song verb ≤ 250 ms; per-frame board queries ≤ 1 ms; printing medians and spread, failing on
the budget. It runs before each phase's commit and at the end; its output (never the corpus) is
recorded in this file's §6.

## 12. Final acceptance

After Phase 8, as separate invocations: a relwithdebinfo build, the full test suites, both
property suites over the corpus, the benchmark at four sizes green, `pre-commit run --all-files`;
clang-tidy when the user asks. Sighting in the running editor on the tiled 4 × 50k package: open
(≤ 2 s), drag a tempo chip at two zooms and confirm the chip tracks the pointer with no stutter,
hold `Alt+→` and `→` runs, type and undo edits late in the song, toggle the 3D preview, save; then
the same package in the game at frame rate. Update plan 41's current state ("after the
projection-cost fix") and the roadmap status board.

## 13. Rollback / abort notes

- Every phase is format-neutral and independently revertible; a reverted phase returns to a
  slower, equally correct editor. Phase 2's sub-phases revert one pass at a time to the whole
  derivation.
- Phase 2 is the risky phase: an incremental derivation that disagrees with from-scratch corrupts
  the picture silently. Land each sub-phase only behind the property suites; if a sub-phase cannot
  be made bit-identical, STOP and re-raise 64-Q2 (checkpoints → segments → restructure) rather
  than loosen the pin to a tolerance.
- Phase 3 keeps `juce::JSON` for `song.json`; if the library fails its pin on any corpus document,
  the old reader stays and 64-Q1 is re-asked with the next candidate.
- Phases 4, 5 and 6 are each deleted, not deferred, if Phase 0 shows them unnecessary.
