# Design review follow-ups — snapshot of 2026-08-21

A snapshot, not a registry: the findings of the two-week design review (commits 2026-08-06 →
2026-08-20, 317 commits, ~33k lines) that were NOT fixed in the review's own change set, ranked,
with the recommended shape for each. Three independent reviewers covered (1) the chart rules /
import / load area, (2) the editor controller, and (3) the shared presentation stack; every claim
below was verified against the code before being recorded. What WAS fixed is in the four commits
that followed W4 (`39fe95e1`, `f088fa08`, `db66cb97` and their tests).

The one-line verdict across all three: **the rules layer is converging on one authority per law
and did so again this week (the normalizer, the fixpoint validator, the resolver clauses); the
layers around it — the controller's verb shape, the two view-state producers, and the renderer's
single draw function — are where restatements still accrete, and each has a named simpler model.**

## Tier 1 — structural, deletes real code, needs a go-ahead

**Progress (2026-08-21, later the same day, with the user's go-ahead):** items 1, 2, 3, and 4
are DONE (`fb815d9a` the slice, `091b8ee8` the toggles, `59f0cd94` the action routing,
`483067c4` the W9-B fold); item 5 (`draw()`) remains. Tier 3: items 9, 10, 11, 12, 14, 15, 17, 18,
19, and 20 are DONE in the commits that follow the fold; items 13 and 16 were investigated and
carry verdicts in place below rather than code. Tier 2 is unmeasured still. Item 9's fix rode the
fold: nothing is ruled about displaying a pinch's right-hand node — both surfaces today show only
its left-hand stop, so the lane now gives a pinch the ordinary fretted head the board already gave
it.

1. **Route the chart intents through `runAction` / `EditorAction`.** The editor has one action
   funnel with availability gating, busy-supersede policy, logging, and the pending-entry
   prologue already in its head (`editor_controller.cpp`, `runAction`). The chart slice is the
   only feature that bypasses it: 41 `EditorAction` cases and not one chart-note case, so the
   chart verbs hand-roll the prologue (21 call sites of `settleChartFretEntry()`), the
   availability guard (9 copies of `arrangement == nullptr || !chart || isBusy() || selection
   empty`), and the commit-point disarm (8 sites). Two of the holes that list produced were fixed
   on 2026-08-21 by moving the prologue into the caret funnel; the list itself is the defect.
   Making the chart intents action cases deletes every hand-placed prologue and guard and makes
   the class unrepresentable. **Recommended next.**
2. **One toggle window, one toggle verb, one property planner.** Eight
   `std::optional<std::vector<ChartNoteKey>>` window fields encode a state that admits one
   (every arming site runs after `applyChartEditPlan`, which disarms them all), and their header
   comment is false about the cost of a new verb (11 touchpoints today: command id, a 7-line
   view case, port method, override, forwarder, recorder override, counter, impl decl/def, field,
   disarm line). `toggleChartNoteFlag`, `toggleChartEmphasis`, and the pick-slide toggle are one
   eight-step body written three times; `planSetNoteFlag` and `planSetEmphasis` are one
   algorithm written twice. Shape: `std::optional<ChartToggleWindow{verb, keys}>`, one
   `onChartTechniqueToggleRequested(ChartTechnique)` intent driven by a descriptor table, and
   one `planSetNoteProperty` over a small property sum type. Roughly −200 lines across four
   files; pairs naturally with item 1.
3. **Give the chart slice its own translation unit and projection module.** `editor_controller.cpp`
   is the assembly file the architecture reserves for construction and cross-slice members, and
   ~2,200 lines of chart verbs, marker, caret, selection, pointer, fret entry, and legato settle
   live in it (every other feature has its `*_handlers.cpp`). The pure pieces —
   `chartFretValueExtendable`, `caretTimeBounds`, `measureJumpPosition`, `chartStartPosition`,
   `adjacentSectionPosition`, `chartNotesForKeys` — are stranded in the controller's anonymous
   namespace and untested; key→index/note resolution exists three times (`selectedNoteIndices`,
   `chartNotesForKeys`, an inline loop in the view-state publisher). A `chart/chart_handlers.cpp`
   and a `chart/chart_navigation.h` take the lot.
4. **Execute W9-B (the shared view-state fold).** Ruled FOLD on 2026-08-13 and still unbuilt;
   the art pass widened the pair (`HighwayFhpView` gained fields, two new 3D-only view types).
   Measured: `TabBendPointView`/`HighwayBendPointView` identical 25/25 lines; the two projection
   loops character-identical for 33 consecutive lines; five struct pairs, five hand-written
   `operator==` bodies. The maintained agreement test (`test_highway_projection.cpp`, "Tab and
   highway projections agree on every shared chart fact") is why this is not a blocker — the
   data cannot silently drift — but the paint layer above it can (item 9). The ruling says fold
   before W10's build; W10 is still unbuilt. Tracked as the interface task and walkthrough W9-B.
5. **Slice `HighwayRenderer::Impl::draw()` per pass.** One 3,966-line function, 22 banner-marked
   passes, 48 lambdas, zero tests — and where every "same question, two answers" renderer defect
   of this review lived (the unbounded scan 25 lines from its bounded twin, the `stable_sort`
   beside the comment forbidding it, the ghosted holder that never dimmed). The head-mark hoist of
   2026-08-20 already proved the return: two decisions extracted, one live divergence found, 30
   lines removed. The architecture's Multi-TU Coordination Objects section blesses the shape.

## Tier 2 — performance, measured by reading, not yet by a build

**First measurement 2026-08-21 (`14a8daf4`):** the game's per-frame trace now carries
`content_cpu_ns` (clock sample through update and render encoding, before the submit), so the
budget is readable from a `--dev` log. RelWithDebInfo, the densest local chart (3,883 notes, 67
sections), 4,000 frames at 144 Hz (6.94 ms period) AT REST at song time 0: p50 0.07 ms, p99
0.19 ms, max 0.57 ms. The playback reading — the one items 6–8 are about — is still owed: the
game's device policy keeps the saved ASIO interface and closes JUCE's substitute when it is
absent, and playback starts on Space, so an unattended run has no song clock. To take it: connect
the interface, `rock-hero.exe --dev --dev-package <pkg> --smoke-frames 6000`, press Space, and
read `content_cpu_ns` per frame from `%APPDATA%\Rock Hero\Rock Hero Game.log` (the worst
one-second window, not the mean, is the number that matters). Nothing below should be tuned
before that reading exists.

6. **Per-frame allocation and whole-song scans in `draw()`** — the full inventory is now in
   `backlog.md` under "Per-frame allocation in the render path" (re-verified 2026-08-21): ~30
   unreserved furniture vectors per frame, a per-ribbon-SEGMENT column vector, five per-note
   vectors on the modulated-tail path, 17 whole-song scans (four growing with playback position),
   `StringLaneStyle` derived per note with six of seven fields unread, slide-run boundaries
   recomputed per sample, and O(S²) tail-shade smoothing. Measure through
   `.agents/rockhero-build.ps1 -Preset relwithdebinfo` before and after; the fixes are
   `FrameScratch` members and the `lower_bound`/`upper_bound` idiom already in the file.
7. **The 2D lane at minimum zoom** — the per-note bracket rescan (O(notes × brackets)), a fresh
   HarfBuzz shaping pass per label chip, a `ScopedSaveState` + clip per visible note whose three
   consumers all early-out, a `TailCenterline` built and discarded for the dominant case, and the
   shapes' visible range scanned from the song start (needs a prefix maximum of span ends, as the
   notes have). Also in `backlog.md`.
8. **Per-keystroke whole-chart work in the editor** — every planner copies the stream, the gate
   builds a second saved-form copy and validates the whole chart, `planSetLegato` resolves the
   whole chart, the pending entry replans in full per digit, every commit rebuilds both the tab
   projection and the 3D scene model, and `updateView()` appears 24 times in the chart region and
   nests (one Alt+click release derives the whole view state three times). The cheap structural
   win is one "publish once at the end of the intent" seam; item 1 provides the place for it.
   Unmeasured; measure on a full-song chart before deciding.

## Tier 3 — smaller restatements and divergences, each a short change

9. **Pinch head shape differs between surfaces.** 2D's `headShapeFor` draws a diamond for ANY
   node; 3D's `highwayNodeHead` asks `at_node`, which excludes the pinch — so a pinch wears a
   diamond in the lane and a rectangle on the highway, and neither site names the other. State
   the divergence at both sites or unify the predicate (item 4 is the natural moment).
10. **The 2D plectrum silhouette is a hand-kept table of the atlas art** (`g_plectrum_half_outline`)
    while 3D graduated the same measurement to load time on 2026-08-17 because hand-kept tables
    drift. Minimum: a test measuring the shipped PNG against the table; better: measure at load.
11. **The `Ink` enum and its positional initializer list** must agree by position with no guard
    (the transparency assert catches omission, not reordering), and `Ink::HeadBacking` has no
    `style[...]` reader. Assign by enumerator.
12. **Theme bypasses in the editor tab view**: two overlays hard-code `Colours::white.withAlpha(0.7f)`
    and one site passes `TabLaneStyle{}` explicitly where its siblings omit it.
13. **`sustainGrowthLimit`'s span coverage is half-open where `chartEffectiveSustains`'s is
    closed.** A note exactly on a span's end blocks growth in one and counts as span-held in the
    other. Harmless today (the held extension at the boundary is zero length) but a third
    statement of "does this span cover this position" — give it one name.
    **VERDICT 2026-08-21: do NOT unify; the boundary conventions differ because the span's END
    is overloaded upstream, and that is the real finding.** The GP importer closes a span at its
    notated ring trimmed to the margin before the next posture, *floored at its own last strum*
    (`gp_chart_builder.cpp`, `close_span`), with an exact-adjacency fallback that ends a crowded
    span *on the next posture's first onset*. So a note sitting exactly on a span end is
    sometimes the span's own last strum and sometimes the next posture's first — no single
    open/closed convention is right for both. Each reader then asks a different question and its
    convention is correct for it: the hold rule (closed) gives a span-end group a zero hold, so
    its closedness never extends anything; the repeat classifier (closed, in seconds) must
    include a handshape's last strum or drops it from repeat treatment (a bug it already fixed
    once); and `sustainGrowthLimit` (half-open) must let a span-end note BLOCK growth, because
    when that note is the next posture's first strum a tail growing across it would ring into the
    next chord. Forcing one predicate would break one of the three. The simplification that would
    make one convention correct is upstream: spans as half-open intervals whose last strum lies
    strictly inside (the importer would then never floor a span onto its last strum), which is a
    40-Q format/import policy decision for the user, not a drive-by.
14. **`planMoveNotes`' comment says a move off the grid is "refused, never clamped"; `advanceGridPosition`
    clamps to measure 1 beat 1**, so a lone note dragged left past bar 1 is silently repositioned.
    Either refuse when the destination clamps or correct the comment.
15. **Flattened sum types in the pending entry**: `ChartFretEntry::began_as_insert` and
    `ChartPendingFretViewState`'s insert/retype fields are a two-case variant stored as a flag plus
    both cases' fields (four `began_as_insert ? :` branches). A `std::variant<InsertAt, Retype>`
    deletes the flag and the branches; the insert half is field-for-field `ChartInsertGhostViewState`.
16. **The grid lattice is implemented twice**: `snapGridPosition` (rational, ties to the earlier
    line) and `MeasureGridWalker` + `seekNearestGridLine` (seconds, ties to the earlier line) — the
    same lattice choosing the nearest line by different metrics, which can land on different
    lines under a tempo change between two bracketing lines. The walker's lattice should be
    `snapGridPosition`; the seconds metric is the legitimately different part. Cost is real (the
    walker also produces ranked visible lines) — a decision, not a drive-by.
    **MEASURED 2026-08-21; not warranted now, pinned instead.** Blast radius: the walker is ~250
    lines of the 508-line `tempo_grid_geometry.cpp` behind 8 public functions and a 601-line
    suite, with 20 including files; its incremental integer stepping is the grid perf fix's hot
    path (`visibleTempoGridLines`). The two metrics are both correct for their question — a
    click snaps to the nearest line ON SCREEN (seconds), a caret steps to the nearest line in
    MUSIC (beats) — and the only other difference by reading is the terminal clamp (the walker
    stops at the terminal anchor, `snapGridPosition` does not; the caret is bounded separately by
    `caretTimeBounds`). What must never differ is which lines EXIST, and that is now a maintained
    test: "Grid stepping and click snapping agree on the lattice" walks the stepper's lattice
    across a meter change, an odd meter, and a tempo change with a step that divides no measure
    (3/16), and holds the walker to the same lines, midpoint by midpoint. If the lattice ever
    changes shape (swing, tuplet grids), unify then — the shape would be one `gridLineAfter` /
    `gridLineAtOrBefore` primitive in grid arithmetic that both the snap and the walker's advance
    read.
17. **`package_description.cpp` parses a chart for the library index without validating or
    normalizing it** — display metadata only today, but a second reader with a different bar.
18. **Leftover one-line restatements**: `attack == NoteAttack::PickSlide` open-coded at ~14 sites
    where the predicate family (`rightHandOnset`, `nodeIsOnNeck`, `legatoClaimable`) has no
    `isScrape`; V1's string range restated without `g_max_chart_strings` in two editor sites;
    the note-order key is a function-local lambda in the validator with no exported name.
19. **Tests**: the two chart test files build the same fixture twice (a builder belongs in the
    shared harness); `test_chart_editing.cpp` is 3,500 lines over six subjects and should split;
    `ChartLegatoSkip` and its tally array have no production consumer until W5 lands.
20. **Docs drift found and not yet fixed**: roadmap plan 57's fret half is moot since the cap fell
    to 24 (`g_max_fret | 30` in its table); `legato-authoring-model.md` still describes a deleted
    `ChartFretEntry::pushed` flag; `highway_metrics.h` overclaims that it holds every authored
    constant (seven world-space distances live in the renderer); `.claude/settings.local.json`
    (gitignored) carries 19 permission rows naming art-pass scripts that no longer exist.

## What the review found clean, for the record

No layering violations in any area (no `common` → `editor`/`game` include, JUCE inside its grant,
no Tracktion outside `common/audio`, zero platform guards in the presentation stack). Float
equality and designated initializers swept clean in the whole delta. Every sighting rig of the art
pass is gone except the three prose residues fixed on 2026-08-21. The wake-token model of the
pending entry was traced through every arm/combine/settle interleaving and found airtight.
