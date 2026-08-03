# Deep-review follow-ups — 2026-08-03

A multi-agent deep review (7 finder dimensions — dead code, duplication, efficiency, design/
conventions, documentation, tests, completeness — each finding adversarially verified by a
dedicated skeptic) ran on 2026-08-03 over everything after commit `813fcb95` (itself the previous
review's cleanup): the four commits `6b5c9894`, `4d33abbf`, `182faedb`, `81ed25bf`, plus the
uncommitted `00-roadmap.md` edit and the untracked `55-pick-slide-notation.md`.

**Outcome.** 42 raw findings, every one CONFIRMED or ADJUSTED by its verifier (none refuted),
deduplicating to the 25 items below. The efficiency dimension came back **clean** (no deadline-path
code changed; the import-path shapes are sorted single passes with log-n lookups). Layering, CI
blind-spot constructs (float compares, designated init, optional access, use-after-move, shadow),
const correctness, and naming in the touched hunks were verified clean. The excised `SlideIn`
notation concept has no remnants, and the reverted pick-slide borrowed-vocabulary experiment left
no residue.

**Status legend.** Each item is `[ ]` open, `[x]` done. Update the box when an item lands.

## CLOSED 2026-08-03 — all 25 items landed

Two passes, both verified through `.agents/rockhero-build.ps1` (build + editor-core/editor-ui
suites green) with clang-format normalized via pre-commit. clang-tidy was not run, per policy.

**Pass 1** (commit `e59f47d8`): R1–R10, R13, R15, R18–R25.

**Pass 2**: R11, R12, R14, R16, R17 — the coverage items, each **mutation-verified**: the arm it
targets was temporarily broken, the new test observed to fail, and the source restored
byte-identical. Killed mutants: dropping the chain-waypoint halving arm; dropping
`keptStrictlyAfterLastWaypoint` from the crush fallback; dropping the `g_max_fret` clamp on an
upward exit; and narrowing the chord-mate skip in the next-note scan.

### Decisions and deviations worth keeping

- **R3** landed as the "no room plants even an agreeing departure" SECTION: a 32nd-note
  coincident-end fixture reaches the departs-and-no-room state directly, so the tie-merged
  hold-exempt fixture the item proposed was unnecessary.
- **R4** took the narrow-the-claim branch for bends after verifying `chart_rules.cpp` orders bend
  points only against the sustain, never against slide waypoints. The `slide_out` bound was a
  real fix (flags-20 test); the bend half is a characterization test plus corrected wording.
- **R6**'s optional `restoredWindowAt` sibling was skipped: the designated-init construction at
  both sites is already clearer than a call would be.
- **R16 — the finding within the finding.** For a chord whose mates trail off in the SAME
  direction, the next-note scan's chord-mate skip is *unobservable*: both mates share the active
  window and the same release travel, so `windowAnchorCovering` returns an identical anchor
  whichever mate wins the shared instant, and the mutant survives. It only becomes observable
  when the mates scrape apart (flags 4 + flags 8), where the exits want opposite windows at one
  instant — that is the fixture the test uses, and it pins that the FIRST member owns the
  instant. Anyone re-testing this area should not expect a same-direction chord fixture to have
  any diagnostic power.
- **R22**'s optional roadmap item-15 append was skipped: plan 25 and item 26a now both carry the
  theme-coordination rule, and a third copy would itself be the duplication the review objects
  to.
- **Unpinned by design**: whether the exit merge yields to or replaces an existing placement is
  not distinguishable by any realistic fixture (chord mates that collide produce identical
  placements in the same-direction case, and the opposite-direction case is settled by insertion
  order). It is left as `insertPlacementIfAbsent` to match the documented "fabricated exits yield
  to real placements" contract.

The items below are retained as the record of what was found and why each fix took the shape it
did; the executor protocol that follows is retained for the next review of this area.

---

## Executor protocol (read first, follow throughout)

This section exists because the executing model tends to make these specific mistakes. Treat each
rule as binding.

1. **Anchor by symbol and quoted snippet, never by line number.** Line numbers below were true at
   authoring time and will drift as items land. Every item quotes the code it targets; locate it
   with `rg -n` on the quoted text or the named function.
2. **No minimal-patch substitutions.** Where an item names a target shape (a helper signature, a
   predicate restructure), implement that shape. Do not leave the old shape in place with a small
   guard bolted on, and do not keep dead variants "for safety" — the Design Quality Bar in
   `CLAUDE.md` governs.
3. **Sweep the class, not the cited site.** Items list *every* known site. After editing, `rg` for
   the construct once more to confirm no site was missed; the compiler will not tell you.
4. **Do not touch what an item does not name.** Specifically off-limits: any pre-existing
   "source"/"source game"/"source corpus" wording outside plan 55 (a retro-scrub is pending a
   separate user decision); `highway_projection.cpp` code (its window changes were comment-only
   and verified consistent); format version numbers (never bumped, project rule); the dense
   why-comment style in `gp_chart_builder.cpp` (preserve voice and user-rule dates; extend, don't
   reflow untouched text).
5. **Catch2 SECTION mechanics.** When folding TEST_CASEs into SECTIONs, hoist ONLY the shared
   `syncs` vector (and optionally the `makeLinearScore` call when the score is identical);
   `push_back`/`buildGpSong`/chart extraction/assertions stay inside each SECTION — code after a
   SECTION block runs after that section, so a shared build would see every section's mutations.
   The file's own precedent is TEST_CASE "derives slide-in ramps from the hand positions".
6. **Verification commands are exact.** Build and test through the helper only:
   `powershell -NoProfile -ExecutionPolicy Bypass -File .\.agents\rockhero-build.ps1 -Targets all -RunTouchedTests`
   (add `-Configure` only after a CMake-graph or stale-Ninja error; `-RunTouchedTests` runs the
   suites whose binaries relinked, which covers the gp-import cases via the editor-core suite).
   Do NOT run clang-tidy (user-triggered only). Do NOT reconfigure CMake — no item here changes
   the build graph (no new files; all helpers are file-local). Finish with pre-commit on the
   touched files to normalize clang-format.
7. **Test assertions use exact `Fraction`/int comparisons** (the payload model is rational
   arithmetic; `==` on `Fraction` is exact and correct). Never introduce floating-point `==`; if a
   float assert is ever needed use Catch2 `WithinULP`. Every magic number in a new assertion gets
   a derivation comment ("1/8 = quarter of the 1/2-beat sustain, floored…"), matching the file's
   idiom.
8. **Report honestly.** If a test you add fails against current code where an item predicted it
   would pass (or vice versa), stop and record what actually happened in this file — the item
   text may rest on a wrong prediction, and silently "fixing" the test defeats the review.
9. **Commits.** Leave everything uncommitted unless the user says otherwise; the working tree
   already carries the user's uncommitted roadmap edit, which items R19/R20 deliberately extend.

---

## Phase 1 — CI-breaking and trivial hygiene (do first)

### R1. [x] Delete the dead test helper `hasFretHandPositionAtFret` — CI `-Werror` break

`rock-hero-editor/core/tests/test_gp_song_importer.cpp`, anonymous namespace (currently lines
192–198):

```cpp
// True when any generated fret-hand position sits at the given fret.
[[nodiscard]] bool hasFretHandPositionAtFret(const common::core::Chart& chart, const int fret)
```

Commit `4d33abbf` removed its only call (`CHECK_FALSE(hasFretHandPositionAtFret(chart, 1))`) when
reframing the released-trail-off test; repo-wide `rg` finds only the definition. It has internal
linkage, and CI compiles tests with JUCE's recommended warning flags (`-Wall` on GCC and Clang —
`JUCEHelperTargets.cmake:53/89`) plus `-Werror` (`cmake/RockHeroBuildPolicy.cmake:61`), so
`-Wunused-function` fails the Linux and macOS legs. MSVC debug is silent — the CLAUDE.md
blind-spot class.

- Delete the comment + function (7 lines). No replacement; the rewritten test asserts positions
  directly via `fretHandPositionAt`.
- Sweep (rule 3): confirm no other helper in the two touched test files lost its last caller in
  the window (`fretHandPositionAt`, `requiredChart`, `fixtureWithReplacement`, `chartOrNull` were
  verified still-called at review time).
- Add the row to CLAUDE.md's "Local Verification Does Not Prove CI" table (its maintenance rule
  invites this; the class was exposed by review before CI could catch it):
  `| -Wunused-function on internal-linkage helpers | unused anonymous-namespace functions accepted | GCC, Clang |`

### R2. [x] Fix the stale "hand planted" cross-reference comment on the departure test

Same file, comment above TEST_CASE "Guitar Pro import rides a trail-off toward the hand's next
move" (currently lines 1953–1957). Its last sentence reads:

> The contradicting case stays the fixed four-fret exit with the hand planted (covered by the
> unpitched-slide test above).

Both claims are superseded model vocabulary: the contradicting/release case *dips and restores*
(it is not planted — "planted" is reserved for the no-room case), and the referenced test was
renamed in the same window to "rides the window through a released trail-off" and now asserts the
dip. Reword to match shipped semantics, e.g.: "The contradicting case keeps the fixed four-fret
exit while the window dips and returns (covered by the released-trail-off test above)."

---

## Phase 2 — Behavior/coherence fixes in `gp_chart_builder.cpp`

### R3. [x] `resolveSlideOutExits`: enforce the "no room → planted" rule on the departure path

The function comment, developer guide rule 9, and the release path all state a trail-off ending at
or past the next onset stays planted — but the check lives only in the `else if`; the `departs`
branch emits an exit placement at `end_position` unconditionally. `normalizeImportedSustains`'
hold exemption skips ALL trimming (including slide-out compression) for a deliberately held note,
so a held slide-out can keep an end at/past the next onset while `departs` is true; the exit
placement then inserts AFTER the real arrival placement and snaps the window backward mid-note.
The double-negative branch shape (`else if` re-testing half its own condition with an inner
`if … continue`) is what hid the gap. Current shape:

```cpp
        if (departs)
        {
            const int travel = downward ? std::min(delta, -2) : std::max(delta, 2);
            note.slide_out->fret = std::clamp(departing + travel, 0, common::core::g_max_fret);
        }
        else if (next_note >= built.size() || !(end_position < built[next_note].note.position))
        {
            // No note follows (the window may rest where the gesture ends), or no room to
            // ride before the next onset: planted.
            if (next_note < built.size())
            {
                continue;
            }
        }
```

Target shape — hoist two named predicates and apply the planted rule uniformly, *before* the
departure classification (a no-room gesture is entirely planted: default four-fret exit fret
stands, no exit placement, no restore):

```cpp
        const bool has_next = next_note < built.size();
        const bool has_room = !has_next || end_position < built[next_note].note.position;
        if (has_next && !has_room)
        {
            // No room to ride before the next onset: the gesture stays planted.
            continue;
        }
        if (departs)
        {
            // ... exit-fret ride (unchanged)
        }
        else if (has_next)
        {
            // ... restore placement (unchanged)
        }
        // No note follows: the window may rest where the gesture ends (exit, no restore).
```

followed by the unchanged exit emission. Update the function comment's "and a trail-off ending at
or past the next onset stays planted" sentence only if its wording no longer matches (the rule
itself is unchanged — this fix makes the code honor it). No developer-guide change needed: the
guide already states the rule this fix enforces.

Regression test (goes with R13): a held slide-out (ring past the next binding onset via a
tie-merged hold, so the trim's hold exemption leaves `slide_out->offset` reaching the next onset)
whose next placement would otherwise classify as a departure — assert NO exit placement is
fabricated and `slide_out->fret` keeps the default four-fret value. If constructing the
hold-exempt fixture proves impossible with the existing score builders, say so in this file and
keep the non-departure planted test of R13 as the guard; the restructure is still required for
coherence with the documented rule.

### R4. [x] Scoop window: honor "kept strictly before any payload" for `slide_out` (and settle bends)

`resolveSlideIns` claims (function comment, mirrored in developer-guide rule 16) the scoop window
is "kept strictly before any payload the note already carries", but the code bounds it only
against the slide chain:

```cpp
        if (!note.slides.empty() && window >= note.slides.front().offset)
        {
            window = note.slides.front().offset * Fraction{1, 2};
        }
```

Two payload kinds are unchecked:

- **`slide_out` (required fix).** A note can carry flags `16|4` (slide-in + slide-out). With
  `note.slides` empty the chain resolver sets `slide_out->offset == note.sustain`; for a short
  note (sustain ≤ 1/8 beat) the scoop window floors to `g_minimum_slide_window` = 1/8 ≥ sustain,
  so the inserted scoop waypoint lands at or after the slide-out end — `chart_rules.cpp` rejects
  exactly this ("slide-out must end after every waypoint"). The superseded moved-head model was
  immune (it shifted `slide_out->offset` by the same lead). Add the symmetric clause after the
  slides clause, using the same halving idiom:

  ```cpp
        if (note.slide_out.has_value() && window >= note.slide_out->offset)
        {
            window = note.slide_out->offset * Fraction{1, 2};
        }
  ```

  Test (goes with R11): a short note carrying flags 20 (16|4) — assert the scoop waypoint offset
  sits strictly before `slide_out->offset` and the import succeeds (the chart passes validation).

- **Bends (decide by evidence, then align spec and code).** `note.bend` is never consulted; the
  deleted moved-head code explicitly shifted bend offsets, so slide-in + bend is a recognized real
  input. First check what `chart_rules.cpp` actually requires between bend points and slide
  waypoints. If validation imposes no ordering between them, do NOT invent bend semantics: narrow
  the two authoritative sentences instead — function comment and developer-guide rule 16 — from
  "any payload the note already carries" to the fret-travel payloads actually bounded ("kept
  strictly before the note's slide chain and trail-off end"), and add a characterization test
  (slide-in flag + bend on one note) pinning that the import succeeds and the bend points are
  untouched. If validation DOES order bends against waypoints, extend the bound to the first bend
  offset with the same halving idiom and treat a zero-offset payload as unplaceable
  (`++unplaceable; continue;` before any mutation). Either way the spec sentence and the code must
  say the same thing when this item closes.

  > Resolution 2026-08-03 (this session): `chart_rules.cpp` orders bend points only against the
  > sustain, not against slide waypoints, so the narrow-the-claim branch was taken: both sentences
  > now read "kept strictly before the note's slide chain and trail-off end", and the
  > characterization test pins bend+scoop coexistence.

---

## Phase 3 — Mechanical consolidation (no behavior change)

All helpers land in `gp_chart_builder.cpp`'s existing anonymous namespace beside
`sustainMarginAt`/`keptStrictlyAfterLastWaypoint`. After R5–R8, build once and run the gp-import
suite once — the suite must pass unchanged (these are pure refactors; any behavior diff is a
defect in the refactor).

### R5. [x] Collapse the four placement-merge loops into two helpers

Four near-identical "merge a fabricated `FretHandPosition` into the sorted `placements` vector"
loops exist, all added in the window: `resolveSlideIns` dip merge (replace-at-collision),
`resolveSlideIns` restore merge, `resolveSlideOutExits` exit merge, `resolveSlideOutExits` restore
merge (the last three byte-identical modulo the loop variable). Extract:

```cpp
// Inserts a fabricated placement, keeping positions unique and ascending; an existing
// placement at the instant wins.
void insertPlacementIfAbsent(std::vector<common::core::FretHandPosition>& placements,
                             const common::core::FretHandPosition& placement);
// Same, but the fabricated placement wins at its instant (a dip owns the scoop's onset).
void upsertPlacement(std::vector<common::core::FretHandPosition>& placements,
                     const common::core::FretHandPosition& placement);
```

Dip merge → `upsertPlacement`; the other three → `insertPlacementIfAbsent`. The call sites keep
their existing explanatory comments ("Merge the fabricated windows…", "Fabricated exits yield…").

### R6. [x] Share the window-anchor clamp (and the restore construction)

The "derive a fabricated window anchor from the active placement by the gesture's travel, clamped
so the window still covers the target fret on the neck" arithmetic appears twice:

- dip anchor: `std::clamp(active->fret - (note.fret - start), std::max(1, start - active->width + 1), start)`
- exit anchor: `std::clamp(active->fret + travel, std::max(1, note.slide_out->fret - active->width + 1), std::max(1, note.slide_out->fret))`

Extract one helper and call it from both (dip passes `travel = start - note.fret`,
`covered_fret = start`; the `std::max(1, covered_fret)` upper bound is a no-op for the dip site,
where `start >= 1` already holds — same result, single owner):

```cpp
// The active window ridden by the gesture's fret travel, clamped so it still covers
// covered_fret on the neck.
[[nodiscard]] int windowAnchorCovering(const common::core::FretHandPosition& active,
                                       int travel, int covered_fret);
```

The two restore constructions (`{position, active->fret, active->width}` in both resolvers) may
collapse into a sibling `restoredWindowAt(position, active)` if it reads better; keep it if it
doesn't.

### R7. [x] Give the two-fret-minimum travel rule a single owner

The 2026-07-29 user rule ("travel never shrinks below what reads as a slide") is spelled three
times: `int start = from_below ? note.fret - 2 : note.fret + 2;` (default start),
`start = note.fret + (from_below ? std::min(delta, -2) : std::max(delta, 2));` (slide-in widening),
and `const int travel = downward ? std::min(delta, -2) : std::max(delta, 2);` (exit-fret widening —
whose comment even says "widened to the slide-in rule's two-fret minimum"). Add:

```cpp
// A slide gesture's travel never shrinks below two frets — the minimum that reads as a
// slide (user rule 2026-07-29). Widens an agreeing delta; supplies the default when the
// hand is still.
constexpr int g_minimum_slide_travel_frets = 2;
[[nodiscard]] constexpr int widenedToMinimumTravel(int delta, bool downward);
```

and use them at all three sites (the default-start site uses the constant).

### R8. [x] (With R5–R7, not standalone) Thin shared lookup for the active-placement idiom

Both resolvers open with the identical five-line
`std::ranges::upper_bound(placements, note.position, std::ranges::less{}, &FretHandPosition::position)`
call. As part of the helper family (its verifier judged it below the defect floor standalone),
wrap the lookup — callers keep their own `begin()` handling, which differs by design
(`resolveSlideIns` still scoops with the default start when no window exists;
`resolveSlideOutExits` skips the gesture entirely).

---

## Phase 4 — Test coverage for the new branching logic

All in `rock-hero-editor/core/tests/test_gp_song_importer.cpp` unless noted. Several gaps are
mutation-survivable today (the named mutant passes the whole suite) — each new case must fail if
its mutant is applied.

### R9. [x] Engage the scoop-window clamp arms strictly (today: no-op equalities only)

The three arms (margin cap, `g_minimum_slide_window` floor, sustain extension) are only hit at
equality, so deleting the cap, the floor, or both passes the suite. Add SECTIONs to the slide-in
TEST_CASE: a two-beat landing (window caps at the 1/4 margin, not 1/2), a sixteenth-note landing
(1/16 window floors up to 1/8), and a sub-1/8-beat landing (note.sustain extended to the floored
window).

### R10. [x] Assert the planted (in-window) scoop fabricates nothing

The headline rule "the hand stays planted while the approach sits inside the active window" has no
placement assertion — a regression fabricating dip+restore for every scoop passes. In the "hand
move consistent with the flag" SECTION, assert `chart.fret_hand_positions` is exactly the natural
walk with no entry at the scoop's end position.

### R11. [x] Cover the chain-waypoint arm and the flags-20 combination

The `window >= note.slides.front().offset → half the waypoint's offset` arm never runs under the
suite. Add: a flags-17 (16|1) slide-in into a following shift-slide note, asserting the scoop
waypoint lands at half the glide waypoint's offset with payload offsets strictly ascending; and
R4's flags-20 short-note case (scoop strictly before the trail-off end).

> Done 2026-08-03 in two halves: "a slide-out on the same short note keeps the scoop strictly
> before it" (flags 20) and "an existing chain waypoint halves the scoop window" (flags 17 — a
> thirty-second head with the landing an eighth of a beat later gives a degenerate shift gap,
> so the junction waypoint sits at 1/16 and the scoop halves it to 1/32). Mutation-verified:
> disabling the halving arm fails the second SECTION.

### R12. [x] Cover the slide-out crush fallback's actual arms

The rewritten fallback (`keptStrictlyAfterLastWaypoint(note, std::max(target, g_minimum_slide_window))`)
is only exercised where `target` is already legal; removing the `std::max` (yielding a zero-length
slide-out) passes the suite. Add: a sixteenth-note trail-off with the next onset a quarter beat
away (offset compresses to 1/8, the smallest legal end); an already-minimal trail-off (end kept);
a waypoint-bearing note (floors strictly after the waypoint).

> Done 2026-08-03. The `std::max` floor and the kept-end arm are pinned by "a trail-off ending
> on the next onset stays planted"; the strict-compress arm was already covered by the ride and
> release SECTIONs (3/4 < the uncompressed 1); and the waypoint floor is now pinned by a
> dedicated TEST_CASE, "floors a crushed trail-off after its last waypoint" — a legato chain
> (flags 2) inheriting the landing's flags-4 trail-off, crowded so the margin target lands
> exactly on the junction and the end steps to 1 + 1/8. Mutation-verified: dropping
> `keptStrictlyAfterLastWaypoint` from the fallback fails that case.

### R13. [x] Cover `resolveSlideOutExits`' planted and song-end arms (+ R3's regression)

Neither documented behavior of the planted/song-end block has coverage. Add: a trail-off on the
song's last note (exit placement exists, no restore); a trail-off whose compressed end coincides
with the next onset (NO exit placement fabricated); and R3's held-departure planted case.

### R14. [x] Exercise the upward (flags 8) direction arms

Every slide-out test uses downward flags 4; the direction-branched riding pass
(`downward ? std::min(delta, -2) : std::max(delta, 2)`, the `g_max_fret` clamps, the high-side
anchor clamp) runs only its downward halves. Mirror at least: an upward trail-off whose next
placement departs upward (exit rides `std::max(delta, 2)`), and an upward release near
`g_max_fret` exercising the high-side clamps.

> Done 2026-08-03 as two SECTIONs of the folded figure TEST_CASE: "a departing hand carries the
> exit fret upward" and "an upward trail-off at the top of the neck clamps to the last fret"
> (fret 28, whose four-fret default would exit at 32). Mutation-verified: removing the
> `g_max_fret` clamp fails the second.

### R15. [x] Pin the dip-replace and restore-yield merge rules

The "flag's direction wins over a contradicting hand move" SECTION reaches the dip-replace path
but asserts only `chart.notes`. Extend it to assert the full `fret_hand_positions` sequence: the
dip REPLACES the natural placement at the scoop's onset (one entry at that instant, not two) and
the restore appears at the scoop's end.

### R16. [x] Cover simultaneous (chord) trail-offs

Two same-onset notes with slide-outs: assert a single exit placement at the shared compressed end
(first-inserted wins, exits yield to existing) and that departure-vs-release classification reads
the onset AFTER the chord, not the chord mate (the `global_beat <=` skip in the next-note scan).

### R17. [x] Fold the three trail-off TEST_CASEs into one with SECTIONs

"rides a trail-off toward the hand's next move", "returns the window after a lingering trail-off",
and "returns the window when the hand never moves" are three variants of the one figure-selection
rule repeating the same scaffold. Fold into one TEST_CASE (e.g. "Guitar Pro import places
trail-off exit windows") with three SECTIONs — hoisting per protocol rule 5 (syncs only; build and
extraction stay per-SECTION), merging the three comment blocks into one. R13/R14's new cases may
join this TEST_CASE as further SECTIONs where the scaffold matches.

---

## Phase 5 — Documentation

Doc-only items; no build or tests. Keep each file's voice; verify every factual claim you write
against the tree the way the review did.

### R18. [x] Plan 55: scrub the forbidden pointer; fix 55-Q2's wording; add per-phase verification

`docs/plans/roadmap/55-pick-slide-notation.md` (untracked — must be clean before any commit):

- **(high)** Bullet 1 of "Why a new notation" reads "exactly how the reference-corpus chart format
  expresses them, since its note vocabulary (`mute`, `palmMute`, `tremolo`, `slideUnpitchTo`) has
  no pick-slide field". "The reference-corpus chart format" is a stand-in pointer to the game this
  project must never reference, and the four backticked identifiers are that format's literal,
  searchable attribute names (they appear nowhere in RockHero code). Rewrite intrinsically, e.g.:
  "Mapping pick slides onto existing chart vocabulary — full mute + tremolo + unpitched
  trail-off, the closest combination the chart can express, since it has no pick-slide field —
  was built and REVERTED the same day: …". Drop the attribution and all four field names.
- 55-Q2 says "fold into plan 40 Phase 5's technique matrix as a cycling toggle". Plan 40 Phase 5
  is "Technique and note-property editing" (never called a technique matrix — that is plan 22/24
  detectability vocabulary, which plan 55 itself uses in that sense at its Non-goals), and plan 40
  explicitly supersedes blind cycling with the §9a apply-where-valid policy. Reword: "fold into
  plan 40 Phase 5's technique-editing surface as a per-note-validated cycle (None → Down → Up)
  under the §9a apply-where-valid policy".
- Phases 1–5 carry no exit criteria or verification commands (sibling plan 54 has both on every
  phase, and Phases 1–2 are declared executable now). Add them in plan 54's shape: build via
  `.agents/rockhero-build.ps1`; touched suites per phase (common core for Phase 1, gp-import for
  Phase 2, paint/pixel for Phase 3, sight pass for Phase 4, the sanctioned bundle for Phase 5).

### R19. [x] Roadmap `00-roadmap.md`: finish registering plan 55

The uncommitted edit added the P55 node, edges, narrative, and status row but:

- The status-board row for 55 sits ABOVE the 54 row (table order …53, 55, 54). Move it below 54.
- No `### docs/plans/roadmap/55-pick-slide-notation.md (gates 55-Q1, 55-Q2)` block exists in
  Decisions-needed, though the section's contract is "every open question from every plan" and
  every other gated plan has one. Add it after the plan-54 block, mirroring 55-Q1's three
  candidate treatments (+ the tab-marker rider) and 55-Q2 with its recommendation, in the
  section's R:-marked format.
- The numbered execution-order list has no item for 55 (plan 54 got item 26a in the same window)
  despite "Phases 1-2 executable now". Add an item sequenced after the plan-25 phases it consumes
  and coordinated with 26a, noting Phases 3–5 wait on 55-Q1/55-Q2.

### R20. [x] Plan 54 accuracy: `IGameSettings` is not "future"; two stale stamps

`docs/plans/roadmap/54-highway-visual-theming.md`:

- Plan 27 Phase 1 (IGameSettings/GameSettings/NullGameSettings) is **complete 2026-07-12** — the
  roadmap's own item 26a (same commit!) says "already landed", and
  `rock-hero-game/core/.../settings/i_game_settings.h` exists. Fix the three spots that say
  otherwise: the inventory's "the game's future `IGameSettings`", the "Upstream (blocking)" list
  entry for plan 27, and Phase 2's "when plan 27 Phase 1 lands; until then the game reads the
  default" (make it an executable step against the existing interface).
- The plan-specific hard rule cites "`editor_theme.h:76–83` records the MSVC cross-TU init-order
  hazard"; the hazard note is at lines 99–102 (76–83 is the paused-cursor Doxygen block). Cite it
  without line numbers: "the function-local-static rationale comment in `editor_theme.h`".
- The inventory section still opens "Verified against code 2026-08-02, `master @ cffd8572` plus
  the uncommitted box-mute work" although the Status block (and commit `81ed25bf`) says it was
  re-verified against `182faedb`. Update the stamp; add the re-verification to the roadmap
  status-board row's right-hand cell too.

### R21. [x] Deduplicate the plan 45 Phase 6 disposition (plan 54 owns it)

The withdrawn-Phase-6 disposition (string-palette half → 54 Phase 4; EditorTheme chrome half →
dropped; "two schemas and two loaders" rationale) is written out in full in BOTH
`docs/plans/roadmap/45-editor-theme-and-string-colors.md` (Phase 6 section) and plan 54's
"Supersedes plan 45 Phase 6 entirely" bullet. It is normative scope text; plan 54 (unstarted,
re-verified before execution, its Phase 4 implements the disposition) should be the single owner.
Reduce plan 45's Phase 6 section to: the withdrawal banner, the 45-Q3-resolves-to-A consequence,
and a pointer to plan 54's bullet.

### R22. [x] Add the theme-color coordination rule to plan 25 itself

Plan 55 says its colors follow "the same coordination rule plan 25 carries" — but plan 25 does not
carry it: commit `81ed25bf` recorded the 54⇢25 rule (new highway colors enter 54's `HighwayTheme`
struct, not new file-scope constants) only in the roadmap narrative and item 26a.
`docs/plans/roadmap/25-note-highway-3d.md` never mentions plan 54 or the theme struct, and its
Phase 5 (gameplay feedback/HUD — the phase most likely to add colors) is still pending. Add a
Constraints bullet (or a Phase 5 note) stating the rule; optionally append it to roadmap Stage 4
item 15, which also lacks the pointer.

### R23. [x] Developer guide `the-project-lifecycle.md`: three alignment fixes

- The normalization-policy preamble points at "(`gp_chart_builder.cpp` — `normalizeImportedSustains`,
  `generateFretHandPositions`, both covered by `test_gp_song_importer.cpp`)", but rule 9's
  trail-off model now lives in `resolveSlideOutExits` and rule 16's scoop model in
  `resolveSlideIns`. Extend the parenthetical to name all four passes (with rule attributions).
- Rule 16 dates the superseded moved-head model 2026-07-28 while the `resolveSlideIns` function
  comment says 2026-07-27. The pre-window doc rule 16 attributed the moved-head model to
  2026-07-28 (2026-07-27 covered the direction-fallback rules), so unify BOTH new sentences on
  **2026-07-28** (edit the code comment).
- Rule 16's "kept strictly before any payload the note already carries" — resolved by R4; make
  the sentence here match whatever R4 landed (see R4's resolution note).

### R24. [x] Flux-note the superseded trail-off claims in the todo plan

`docs/plans/todo/fhp-corpus-derived-generation.md` states "(unpitched trail-offs deliberately
never move the hand); see normalization policy rule 9" and "Unpitched trail-offs get no board
furniture at all" — both now the OPPOSITE of shipped behavior (rule 9 rewritten 2026-08-02; the
window always rides trail-offs via exit/dip/restore placements). The todo bucket tolerates lag,
but the maintained guide points at this file and both sentences cite rule 9 by number while
contradicting it. Add a short dated correction at both spots (do not rewrite the plan): the
2026-08-02 user rules superseded the never-moves model.

### R25. [x] Backlog entry: re-import GP-derived projects saved before the scoop model

`docs/tracking/backlog.md`: the scoop model changed what import writes (onsets no longer move
early; sustains/waypoints differ; FHP tracks gain dip/exit/restore placements), so GP-derived
projects/packages saved before `6b5c9894` still embed moved-head slide-ins and windowless
trail-off FHP tracks. Add the entry scoped to **GP-derived saves only** (e.g. the Van Halen import
project that motivated plan 55) — explicitly NOT the 39-package converter-sourced `.rock` corpus,
which never passed through these resolvers (per `the-project-lifecycle.md`: the GP normalization
policy applies to GP import only).

---

## Phase 6 — Verification and closure

1. After Phases 1–4: `powershell -File .agents/rockhero-build.ps1` (build), then
   `powershell -File .agents/rockhero-build.ps1 -Test -TestFilter gp-import`, then the full
   editor-core and common-core suites once. All green, including every new SECTION.
2. Re-read every touched hunk against the CLAUDE.md CI blind-spot list (float `==`, designated
   init, optional access, use-after-move, shadow, unused internal-linkage functions — the class R1
   adds).
3. `git diff` every touched file end to end; check each R-item's box above; record any deviation
   (protocol rule 8) inline in the item.
4. Do NOT run clang-tidy. Do NOT commit unless the user has asked.
