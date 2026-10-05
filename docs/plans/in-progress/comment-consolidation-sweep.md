# Comment consolidation sweep

**Status:** every decision is ruled and the standard is signed (2026-10-04). Regular comments are
mostly one-line notes, with multi-line comments for whole-function descriptions and real
explanation (section 3); Doxygen keeps its own, more descriptive standard. The signed reference
files are `engine_tone_timeline.cpp` (regular comments), and `busy_overlay.h` and `highway_light.h`
(Doxygen), in `C:/__MAIN__/Coding/__scratch__/comment-sweep-examples/level3/` beside their
originals. Phase 0 copies their before/after pairs into the `comment-editor` agent definition.
Next step: Phase 0, run by a Fable session in this worktree.

## 1. Goal

Every comment in the project states the current truth once, concisely, at the place that owns it.
History belongs in git. A comment carries history only in the rare case where the shape of the
code cannot be understood without it, such as a workaround for a framework defect or a constraint
imposed by a format that has already shipped. Even then it states that history as the reason,
undated.

**Scope: every project-owned file that carries comments.** That is all of `rock-hero-common/`,
`rock-hero-editor/` and `rock-hero-game/` (headers, sources, tests and `.sc` shaders), every
project-owned CMake file, and the scripts under `.agents/`, `scripts/` and `.github/`. Excluded:
`external/`, the `project-config/` submodule, generated files, and the Markdown under `docs/`
(prose documents are a separate concern; see D1).

## 2. Why the last three sweeps did not hold

Three earlier sweeps ran on 2026-08-05, 2026-09-08 and 2026-10-02 (`4f89546d`). The last one
removed dated markers successfully: ten dated lines remain in code. But wherever a stacked passage
carried a reason, it restated that passage in the present tense instead of consolidating it, so the
length stayed. The causes are structural:

- **The conventions push toward more.** `documentation-conventions.md` says "When in doubt,
  comment" and "the cost of a comment that later becomes stale is small", and it requires a comment
  on every `.cpp` function. It says nothing about rewriting instead of appending, nothing about
  history, and sets no length limit.
- **Every change appends.** An agent that changes code edits the passage it touched and adds the
  new reason beside the old one, matching the surrounding density.
- **Nothing checks.** No gate notices a comment growing.

So the sweep is paired with prevention (Phase 0). Without it the tree drifts back, as it has three
times already.

**Measured 2026-10-04** (836 C++ files, 281k lines):

- About 35k `//` lines in 13.8k blocks. Block length: median 2 lines, 90th percentile 5,
  99th percentile 13, maximum 59. There are 249 blocks of 8 lines or more.
- 7.1k Doxygen blocks, about 20k lines in total.
- Roughly 20% of all lines are comments.

| Area | Comment lines | Files |
|---|---|---|
| `rock-hero-editor/core` (src / include / tests) | 10.8k / 4.7k / 8.8k | 119 / 50 / 80 |
| `rock-hero-common/core` (include / src / tests) | 8.9k / 2.5k / 5.1k | 61 / 47 / 36 |
| `rock-hero-editor/ui` (src / tests / include) | 7.6k / 1.6k / 0.2k | 96 / 26 / 2 |
| `rock-hero-common/audio` (include / src / tests) | 3.5k / 3.1k / 1.4k | 52 / 78 / 40 |
| `rock-hero-common/ui` (src / include / tests) | 3.9k / 1.4k / 1.2k | 38 / 6 / 12 |
| `rock-hero-game` (all) | 3.1k | 97 |

The problem has two forms. A few hundred long blocks are stacked essays. Beyond those, the density
is a 3-5 line comment on almost every small helper.

## 3. Consolidation levels (PICK ONE)

The same two real comments are shown at each level. The reduction figures are estimates; the pilot
measures the real ones.

**Example A** is a short helper comment, `rock-hero-common/core/src/chart/chart.cpp:114`
(6 lines):

```cpp
// Whether a bend point at `offset` stating `semitones` would change the curve. The curve is
// interpolated like the path, but its values are doubles, so only the FLAT case is judged, by exact
// equality: a value that repeats the statement before it and is repeated by the one after (or is
// the last, past which the curve holds) lies on a flat segment and says nothing. A point on a
// sloped segment is kept, whether or not it is collinear — no information is ever stripped by an
// approximate test.
```

**Example B** is a stacked block, `rock-hero-common/ui/src/highway/highway_renderer.cpp:4116`
(32 lines). Each later change appended a paragraph. It is not reproduced here; read it in place.

### Level 1: Scrub

Delete history, attributions and any sentence that repeats another. Never shorten a reason.

- A: unchanged (it has no history).
- B: about 28 lines.
- Estimated reduction: 5-15%. This is what the 2026-10-02 sweep did, and it does not fix the
  complaint.

### Level 2: Rewrite once

Rewrite every comment from scratch as the current truth. Each reason is stated once. Restatements
of the code are dropped. An explanation duplicated across files becomes a one-line pointer to its
owner. There is no hard length cap.

A (4 lines):

```cpp
// Whether a bend point at `offset` stating `semitones` would change the curve. Bend values are
// doubles, so only the flat case is judged, by exact equality: a value equal to both neighbours
// (or the last, past which the curve holds) adds nothing. Sloped points are always kept, so no
// approximate test ever strips information.
```

B (11 lines):

```cpp
// Sustain tail: a three-band ribbon from the hit line (while sounding) or the onset to the ink
// end; technique notes modulate its centerline. The shared visible-span clamp
// (highway_floor_geometry.h) handles zero-length, passed and horizon-clipped tails at once.
//
// A RESTING tail draws its remainder only inside the curtain: one reveal lead deep, full at the
// hit line and fading out at its outer edge. A note still in flight carries an identical local
// curtain anchored at its resting landmark (the head for a whole-tail rest, the play-out point for
// a split one), so the two coincide at the hit line and the handoff changes nothing. The curtain
// multiplies into the per-position alpha envelope the tip and onset ramps already ride, so no
// batch changes. Near the song's start the projection shortens the lead, and a note at the very
// first instant gets none (chart_projection.cpp).
```

Estimated reduction: 30-45%.

### Level 3: Budgeted (recommended)

Level 2, plus a shape that any agent can check (ruled: most comments are small notes):

- **Most comments are one-line notes.** A comment inside a body says its one thing in one line.
- **Multi-line comments stay common in two places:** a comment describing a whole function or
  type (up to 4 lines), and a comment that genuinely needs explaining, such as a reason that spans
  several facts or a warning against a plausible but wrong refactor (up to 3 lines inline).
- These ceilings are what the pre-commit check enforces (D3). The one-line default is the writing
  standard, which reviewers hold workers to.
- A longer comment is allowed only where it states a law or invariant the code cannot show, and
  then only at the rule's owner (its header Doxygen or the developer guide). Every other site
  names the owner.

A (2 lines):

```cpp
// Whether a bend point would change the curve. Only an exactly flat point (equal to both
// neighbours) is redundant; sloped points are kept, since no approximate test may strip data.
```

B (4 lines):

```cpp
// Sustain tail: a three-band ribbon from the hit line or onset to the ink end, clamped by
// highway_floor_geometry.h. A resting tail draws its remainder only inside the reveal curtain; a
// note in flight carries an identical curtain at its resting landmark, so the handoff at the hit
// line is seamless. The curtain rides the per-position alpha envelope.
```

Estimated reduction: 50-60%.

**The budget binds regular comments only. Doxygen has its own standard.** Doxygen is reference
documentation, so it stays descriptive: what the entity is, its units and contract, its edge cases,
which parts of the code read it, and why it is shaped this way when a caller needs to know. Doxygen
loses only what does not belong in reference documentation: history, the trail of how a decision
was reached, and narration of the implementation, which belongs in the `.cpp`. The worked header
example (`highway_light.h`) changes little for that reason: "a look chosen on sight" and "a
sighting knob" go, and the descriptions stay. The signed reference source example
is `engine_tone_timeline.cpp`: 44 comment lines become 18. Seven of its ten notes inside function
bodies are one line; the three multi-line comments are the three real explanations (restoring gains
on release, the two-point envelope minimum, and the function comments). The earlier
`highway_projection.cpp` example predates the one-line default and is looser than the standard, so
it is NOT a reference; Phase 0 must not copy it into the agent definition.

**Why this level is recommended.** Every sweep without a measurable limit has drifted back. A
budget is the one rule a worker, a reviewer and a future agent can all check. The law exception
keeps the explanations that really are load-bearing, so it does not strip for the sake of
stripping.

### Level 4: Minimal

Comment only where a refactor would break something, or where the behavior would surprise a
reader. One line is typical; everything else relies on names and the design documents.

- A (1 line): `// True unless the point lies on an exactly flat stretch of the bend curve.`
- B (2 lines): `// Sustain tail ribbon; a resting tail shows only inside the reveal curtain (the
  tail law, chart_presentation.h).`
- Estimated reduction: 70% or more.

This level conflicts with "When in Doubt, Comment" and with the earlier instruction not to strip
for stripping's sake. It is not recommended.

## 4. Other decisions

- **D1: Doxygen prose (ruled).** The Doxygen blocks are in the sweep under their own descriptive
  standard (section 3), not the regular-comment budget. They lose history and implementation
  narration and keep everything a reader of the generated documentation needs. Required fields
  (`\brief`, every `\param`, `\tparam` and `\return`, and every enumerator) stay in every case.
  Markdown under `docs/` stays out of scope.
- **D2: Conventions change (ruled: yes).** `docs/design/documentation-conventions.md` gains a
  section, "Comments State the Current Truth": rewrite instead of append, history belongs in git
  (with the rare exception in section 1), and the two-tier standard (descriptive Doxygen, capped
  regular comments). "The cost of a stale comment is small" is removed, because a stale stacked
  comment is exactly the cost this sweep is paying.
- **D3: Mechanical check (ruled: yes).** `scripts/verify-project-conventions.py`, which already runs
  in pre-commit, rejects dates (`YYYY-MM-DD`) inside comments and flags regular comment blocks over
  the cap; Doxygen is exempt from the length check. In the rare case where history explains the
  code, the comment cites the commit hash, never a date, so the date check has no exception to
  carve out.
- **D4: The per-function comment mandate (ruled: kept).** Every `.cpp` function keeps a comment
  unless it is explicitly trivial. The cap makes those comments short; it never removes one.

## 5. Rules every worker applies

1. **Rewrite, never append.** The result reads as if written today by someone who knows the
   current code.
2. **No history.** Remove dates, rulings, "sighted", "used to", "now", "no longer", "the old X",
   plan, phase, review and task IDs, commit hashes and attributions. The only exception is the one
   in section 1, written as the reason and citing a commit hash, never a date. References to numbered rules stay, because they
   name rules, not events.
3. **One reason, one place.** A rule is explained at its owner. Every other site gets one line
   naming that owner.
4. **Verify every claim against the code.** Correct a comment that is wrong. Never change code.
   List every correction in the report.
5. **Keep what is load-bearing.** This means the situations listed under "When in Doubt, Comment":
   a load-bearing constant, a refactor that would silently break, a library bridge, a remote
   invariant, surprising behavior, code that looks wrong but is intended. Delete only restatements
   of the code.
6. **Keep the Doxygen structure.** Required fields stay, as does the Doxygen-versus-regular comment
   placement.
7. **Mechanics.** Wrap lines at 100 columns by hand, never `/*` inside a block comment, and never a
   `//` line ending in a backslash (`-Wcomment` is part of `-Wall`, so CI would fail).
8. **The chosen level's budget, for regular comments.** Doxygen follows the descriptive standard in
   section 3: a complete contract, minus history and implementation narration.
9. **Move a why to where it applies.** A reason about one line belongs on that line, not in the
   function's header comment. The worked example moves the "ramp from the previous arrival, never a
   release" warning onto the ramp assignment it guards.

## 6. Phase 0: prevention (the sweep branch's first commit)

It lands as the branch's first commit, before any worker runs, so every worker follows it. It
reaches master when the branch merges, which keeps master undisturbed while other work runs there.

- The D2 conventions section.
- `.claude/agents/comment-editor.md`. There is **no existing commenting agent**; the closest ones
  are `simplicity-expert` (design) and `naming-expert` (identifiers). It is a model-`opus` worker
  that encodes section 5, the two-tier standard, the gates in section 9, and the report format.
  It carries the signed before/after pairs from the worked examples as its reference standard. It
  remains the standing agent for any future comment work.
- One line in `CLAUDE.md` under Agent Process: any brief that edits code carries "rewrite comments
  to the current truth; never append".
- D3, if signed off.

## 7. Setup

```sh
git worktree add ../RockHero-comment-sweep -b comment-sweep master
```

The Fable session starts *inside* the worktree. It commits Phase 0 first, then restarts, so the new
`comment-editor` agent is loaded before any worker runs. The
sweep itself needs no build directory, because the token gate (section 9) proves the code is
unchanged without compiling.

## 8. Execution (Fable orchestrates, Opus works)

1. **Pilot.** Fable picks three files: a source file with long blocks, a header heavy with
   Doxygen, and a test file. It runs `comment-editor` on them and reports the before/after line
   counts and the tokens spent per file. **You sign off on the look.** Fable then tunes the agent
   definition and estimates the cost of the full sweep before fanning out.
2. **Shards.** Fable cuts shards of about 3-4k comment lines, roughly 20 in all. A header stays in
   the same shard as its sources and tests, so a rule's owner and its users are seen together.
   Large single files get their own shard: `test_gp_song_importer.cpp` (1.7k comment lines),
   `highway_renderer.cpp` (1.7k), `test_chart_shapes.cpp` (1.5k) and `gp_chart_builder.cpp`
   (1.1k). CMake, shaders and scripts make one shard.
3. **Waves.** Up to eight workers run concurrently, through the Agent tool or a Workflow script.
   Each worker:
   - edits its files in place;
   - runs the per-file gates;
   - reports, per file, the lines before and after, any claims corrected, any explanation it found
     duplicated in another shard, and anything it could not verify.

   Workers never commit.
4. **Shard review.** For each shard Fable re-runs the gates, reads the largest rewrites and a random
   sample, settles the items the worker could not verify, and commits **one commit per shard**, so
   any shard can be reverted on its own.
5. **Cross-shard pass.** Fable consolidates the reported duplicates to their owners.

## 9. Verification gates

Proven on 2026-10-04 against the two worked examples: comment-only rewrites, including Doxygen
blocks, compare equal, while a code edit and a constant edit are both caught. The gates:

1. **Token gate (per file).** The comment-free raw token stream is identical before and after.
   Clang prints a multi-line comment as ONE token spread over several lines, so the filter must
   read whole records (each ends at its `Loc=`). A line-based `grep` leaks the rest of every block
   comment into the comparison:

   ```sh
   toks() { clang -x c++ -std=c++23 -fsyntax-only -Xclang -dump-raw-tokens "$1" 2>&1 | awk '
     {rec = rec $0 "\n"}
     /Loc=</ { if (rec !~ /^(comment|unknown) /) { sub(/[ \t]*Loc=<[^>]*>[ \t]*\n$/, "", rec);
               gsub(/[ \t]+\[(StartOfLine|LeadingSpace)\]/, "", rec); print rec } rec = "" }'; }
   ```

   The gate must also assert that the stream is non-empty. Two traps make this necessary:
   `clang -fpreprocessed` is not a driver flag in clang 22, and a misspelled flag fails silently, so
   two empty outputs compare equal. Each worker runs one negative control (a deliberate code edit
   must fail the gate) before trusting it. CMake, shader and script files are diffed with their
   comment lines filtered out.
2. **Mechanics (per file).** No changed line over 100 columns, counted in characters, not bytes,
   since an em dash is three bytes; no `/*` inside a block comment, no
   `//` line ending in a backslash.
3. **History grep.** No dates or listed history markers remain, unless the report justifies one.
4. **Doxygen.** Run `doxygen` before and after. The warning count must not grow; the build enables
   `WARN_IF_UNDOCUMENTED` and `WARN_NO_PARAMDOC`.
5. **pre-commit**, once per shard.
6. **Final.** After master is merged in: one build and the full test suites in the worktree
   (configured once), then a PR so CI runs GCC, Clang and clang-cl.

## 10. Landing

- Merge master into the branch at the end. For each file that conflicts, take master's version and
  re-run the worker on that file alone, then re-gate it. The sweep is mechanical, so redoing a file
  is cheaper than hand-merging its comments.
- Open the PR with the before/after measurements from section 2 re-taken. You merge and push.
