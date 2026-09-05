# The Finalized FHP Derivation Algorithm (#167)

**Status: PROPOSED 2026-09-05, awaiting the user's sign-off. Not yet implemented.**

This is the outcome of the FHP accuracy push: a ground-truth dataset of 4,555 professionally
reviewed arrangements (442,323 authored hand-position anchors; the dataset lives in local
scratch, outside the repo, per the corpus firewall), two independent statistical analyses run
against it, a ten-facet research pass over position pedagogy / biomechanics / the
automatic-fingering literature / charting practice, and a final expert synthesis in which every
rule below was measured, not argued. It supersedes the study in
`docs/plans/todo/fhp-corpus-derived-generation.md` (whose "~35% of moves are forced" and 0.8 s
phrase-rest rule are both now refuted) and, once implemented, replaces the body of
`generateFretHandPositions` in `rock-hero-editor/core/src/project/gp_chart_builder.cpp`.

The headline model shift: the authored anchor stream is not a coverage-satisfier with a shift
heuristic. It is a **figure-segmented floor tracker** — partition the fretted onset stream into
runs, place each run's anchor on its own floor (the index-finger fret), and hold the window
while written rings sound. Where floor placement and minimal-shift disagree, professional charts
pick the floor 82% to 3%: minimal shift — the current generator's core — is the wrong prior.

## The law, as stated (the charter-facing model — user legibility ruling, 2026-09-05)

The user tested the rule count against the span-derivation complexity lesson: a charter
watching FHPs recalculate around an override must be able to predict the result. The law
passes because it is three sentences, and the numbered list below is only the implementer's
execution order — never the thing a charter is taught:

> **Cut the song into figures at boundaries you can see** — a chord being struck, a bar line,
> a section start, a marker, or the grip outgrowing the hand. **Put the hand at each figure's
> floor**, spanning the grip, never narrower than four. **Two exceptions, both of which only
> ever REMOVE motion**: the hand never moves while a written note still rings, and a one-fret
> figure the standing window already covers does not move it.

Everything else below is vocabulary (what counts as a note the hand plays) and physics
(clamps) and don't-say-it-twice bookkeeping. The legibility property that carries the ruling
is MEMORYLESSNESS: each figure's window is a pure function of that figure's own notes, so an
override's blast radius is exactly the figure it heads plus the previous figure's end — unlike
the replaced minimal-shift walk, whose every window depended on the chain of prior windows and
whose edits could ripple to the end of the song. Every trigger in this law is a visible
timeline object (a chord, a barline, a section mark, a ring, the lowest fret); the
span-derivation trap was rules keyed to invisible constructs, and none exist here. The rules
below are also what SURVIVED measurement, not what accumulated: the rest rule, carry policies,
box-coherence, and approach-direction were all killed or refused by ablation, and each
survivor's removal price is recorded (hysteresis −1.6 pts AND more churn; chord break −11;
the ring rule is physics).

## The algorithm (the implementer's execution order)

One O(n) forward pass over onset groups, bounded lookahead (the run scan), in execution order.
Tags: **[LAW]** fixed in code; **[PARAM]** a named parameter with a default.

1. **[LAW — 99.97% zone invariant]** A coverage group is the fretted, non-tap notes of one onset
   instant; open strings and tapped notes never constrain the window.
2. **[LAW — 9,958/9,958 corpus slides]** A pitched slide contributes its destination fret as a
   coverage demand at the slide's end; the slide is the sanctioned carrier of any shift it
   causes.
3. **[LAW — figure segmentation]** Close the run at: a chord onset (≥2 fretted notes), a bar
   line, a section/phrase start where one exists, or hull overflow past max(4, current hull).
4. **[PARAM rest_break_seconds = off; range off / 0.5–3.0]** No rest break — a rest's measured
   move-hazard is 10–14%, and every tested value loses agreement (−0.17 at 0.5 s). A rest is
   where a move *lands*, not what triggers one.
5. **[LAW — hysteresis, single-peaked sweep: hull 0→86.2, 1→88.0, 2→85.2, any→79.7]** A run
   whose fretted material lies on **one fret** the standing window covers at the standing width
   emits nothing — a restatement, not a move.
6. **[LAW — pinned-finger certainty + 22× measured suppression]** No unforced re-derivation
   while a plain fretted **written** ring struck earlier still sounds; the move re-derives at the
   next run head after the ring ends. A forced escape still moves (99.20% of forced onsets
   move).
7. **[LAW — anchor ≤ floor 99.902%, == floor 88.8%; beats minimal-shift 82.1 : 3.1 where they
   differ]** Anchor = the run's minimum fretted fret (the index-finger floor). Never above it.
8. **[LAW — 98.35% default-4 tap-corrected; wide anchors hug the hull 87.1%]** width has two
   meanings, separated by the truth amendment (user-ruled 2026-09-05): **capacity** — the
   segmentation reach test keeps max(4, …), so runs/moves/coupling are width-amendment-blind —
   and **statement** — the emitted window is max(4, run hull) in fingerless mode, never
   widened to absorb a held note; where fingering pins BOTH ends of the hand (rule 7's index
   seat + finger 4 planted at the hull top) the emitted width states the proven extent, below
   4 when true. A deliberate deviation from source-game convention (its width-4 floor is
   measured pure convention: 99.93% on exactly the grips that disprove it); same standing as
   the tap exclusion. Fingerless mode is unchanged per the zero-authored-input invariant.
   SIGHTING-GATED at first activation (a compact GMaj7-class grip spans 2 frets and may read
   oddly; keeping the 4-minimum after sighting is a legitimate outcome that would stand as a
   sighted display ruling of our own) — structurally a Phase 2/3 feature, since sub-4 cannot
   fire without fingering.
9. **[LAW — 13/8,465 corpus exceptions]** Clamp anchor to [capo+1, 24−width+1], floor winning.
10. **[LAW — zero-lead 95.5%; "early" authored anchors are leading open strings 73.75%]** Emit
    at the first note the run serves, counting leading open strings; never anticipate further.
11. **[LAW — 14.2% of authored transitions are bookkeeping]** Emit only when (fret, width)
    changes. This gate is final over the WHOLE stream — authored inputs (markers, fingers,
    window statements) are derivation inputs, never output patches, so they funnel through it
    too: two consecutive identical FHPs are unrepresentable (user-verified 2026-09-05). The
    load-bearing consequence: every emitted transition IS a real hand move by definition, so
    the let-ring coupling can never clip a span on a phantom shift, and a marker that changes
    nothing still breaks its run and its span while the FHP stream stays silent. (The source
    format's required phrase-start restatements are bookkeeping we deliberately do not
    inherit.)

### Measured score of exactly this algorithm (all 4,555 arrangements)

| | exact | within-1 | churn (authored) | coverage |
|---|---|---|---|---|
| **ALL** | **88.03%** | **94.81%** | 7.27 (7.58) | 99.938% |
| Lead | 88.67% | 95.12% | 8.39 (8.44) | 99.891% |
| Rhythm | 91.51% | 97.26% | 5.52 (5.48) | 99.996% |
| Bass | 77.15% | 87.45% | 9.53 (11.37) | 99.883% |

Churn = real moves per 100 fretted notes. Per-arrangement median 88.74%. Baselines on the same
harness: analyst A's candidate 86.32, analyst B's 86.13, the shipped generator ~70, the oracle
on authored segmentation 95.2 (segmentation is the remaining headroom). Always report **two
numbers**: coverage (a *possible* hand, ~99.9% everywhere) and exact agreement (the *charter's*
hand, 88) — the gap between them is the measured opinion space, and a single "correctness"
number for derived positions lies.

### Honest residuals

- ~5% non-index-finger placement: a fingering datum the chart model does not carry (see the
  fingering extension below). Fingering-aware placement measured +1.48 on the arrangements that
  state fingers.
- ~4–6% box-coherence re-anchors: the mechanism is confirmed (among featureless unforced moves,
  the landing equals the anchor of the figure's previous occurrence 65.0% vs a 25.5% null — a
  2.5× lift on every part), but every deterministic form tested over-triggers and loses ~2 pts.
  Mechanism named, rule refused.
- Bass trails (77.15): the 3-fret low-frame idiom is visible in the error mass (24.4% of bass
  errors sit at floor −2) but has no derivable trigger; the open-string-pivot hypothesis is not
  bass-specific; the higher unforced tolerance is structural. Same algorithm for bass, residual
  stated, no fake parameter.

## The profiles verdict

The question: should users save FHP "profiles" — parameter sets that fully define every non-law
case, tuned to a signature look, possibly artist-fitted — or does one sensible default win?

Measured answer: **one school, one sensible default; no artist presets.** The corpus shows no
charter/artist clustering (ICC ≤ 0.25 on every style metric; the visible extremes are content —
open-position strumming vs thrash — not style). The honest knob inventory collapsed under
measurement: `hysteresis_hull` is a law (every other value loses 2–8 pts), `width_default` is an
invariant, the placement-offset split is finger-shaped rather than parameter-shaped (no constant
or conditioner beats 0), no bass-specific parameter earned its keep, and a churn/style slider
was refused because synthetic unforced moves cannot be placed better than 65% even knowing the
mechanism — a slider would inject noise, not style. The surviving parameters:
`rest_break_seconds` (default off) and `section_breaks` (default on, ±0.02). Artist-fitted
presets would imitate repertoire, not hand style: fake.

Recommendation: laws fixed in code; the named constants and the two parameters reachable per the
standing no-hidden-style-constants requirement; **no profile UI**. The charter's signature look
lives in hand-editing the generated stream in the editor — and, later, per-note fingering, the
one lever that measurably personalizes output. (User ruling pending; the profiles product idea
stays recorded and can be revisited if a second charting school ever shows up in data.)

## What changes in RockHero (implementation deltas)

- **Deleted** from `generateFretHandPositions`: the minimal-shift drag core, the boundary/rest
  re-anchor machinery and `g_fhp_phrase_rest_seconds`, the pinned-finger window union, the
  held-hull slide reshape (sub-4 widths disappear), and the drag special cases. **Replaced by**
  the 11-rule pass — net code deletion. Slide keyframes become ordinary coverage events: the run
  walk re-derives at the destination floor, so "the slide is the shift" needs no special code.
- **Data vs render**: anchors state on-onset truth (rule 10); the highway and camera LEAD by
  ~0.8–1.2 s and animate moves over a fixed ~300–450 ms minimum-jerk curve (measured human shift
  kinematics; smootherstep is exactly that profile) — a projection-layer transform, never early
  anchors. This also answers the open camera-timing question (#153): the drift speed was right,
  the timing must lead.
- **The let-ring coupling** consumes the emitted FHP stream: derived spans clip at any emitted
  move not carried by a slide. Rule 6 concentrates those clips where a hand demonstrably moved,
  which is what makes the coupling honest.
- **Acceptance harness**: the sighted-offender worklist (pins first, in the scratch reference
  registry) re-measured after the reimplementation; sighting #1 traces to the deleted
  rest-break + minimal-shift pair. Ring suppression's real effect must be re-measured on GP
  imports, whose written durations are dense where the ground-truth corpus's sustain field is
  sparse.
- **Fingering extension (future)**: an optional finger on `ChartNote`; rule 7 becomes
  `floor − (finger − 1)` where a finger is stated — one substitution, +1.5–1.9 measured, and it
  subsumes the placement-offset "style" entirely. Never derive fingers heuristically: position
  is objective (97% expert agreement), finger is not (67%).

## Fingering, grip templates, and the derived-stream proposal (2026-09-05)

The fingering conversation converged with the span-marker redesign's standing template model
(see "The template's content, the picker, and the FHP coupling" in
`docs/plans/todo/span-marker-redesign.md` — the leading record for the grip/dictionary side):

- **Fingering inputs to this algorithm**: a marked span's resolved template pins its run's
  anchor (`anchor = floor − (floor's finger − 1)`; a barre pins at the barre fret), and the
  template's unsounded stated stops JOIN the coverage demand; a free note may carry an optional
  authored finger with the same anchor meaning. Stacked fingerings are first-class (the standard
  power chord's 1-3-4), which is why fingering carries information the anchor alone cannot.
- **PROPOSED — the derived stream** (open ruling 4): FHPs become fully derived at read and the
  stored `fhps` stream leaves the format — the same move spans made, for the same reason: a
  stream generated at import drifts from the notes on every edit, a two-authority defect. The
  stored residue is exactly the opinion layer: marker template references and free-note fingers.
  The **override language is fingering itself**, which spans the complete legal space for
  fretted material (anchor ∈ {floor … floor−3} ⇔ finger 1–4 on the floor) and cannot state an
  impossible hand — "override only if valid" met structurally, with no validity guards.
  **Manual FHP specification (user, 2026-09-05)** is part of the authored-statements input and
  splits by context so valid-by-construction holds everywhere: over FRETTED material the manual
  language is markers + fingering — a marker forces a statement boundary ("the hand re-states
  here"), a finger pins the anchor ("at this position"), together spanning the entire legal
  space with impossible windows unspellable; over OPEN/SILENT stretches a direct WINDOW
  STATEMENT record is the language (fingering has no floor to attach to, and with no fretted
  stops under it a raw statement cannot violate coverage — only the clamp, enforced by range).
  Corpus support for the open-stretch record: professionally authored anchors serve no notes at
  all 2.1% of the time (3.3% nothing fretted) — placing the displayed hand over intros and open
  riffs is a real authoring act derivation cannot infer. Statements are derivation INPUTS, never
  output patches: one bounds and pins the run it heads, upstream ends there, downstream resumes
  at the next boundary, rule 11 dedups redundant restatements, and every note edit re-derives
  coherently around standing statements. A statement placed mid-ring would author the
  certainty-class impossible hand rule 6 forbids — the verb refuses it (hard-stop temperament,
  as the span-boundary gestures). Width stays law-derived, no override.
  Tooling that authors `fhps` today (the sighting reels, the external converter — #78's window)
  switches to authoring fingering statements. The one-arrow order at read:
  notes + authored statements (free-note fingers, markers, template references, claims) →
  FHP derivation → span/ring derivation. The order cannot flip — spans CONSUME FHP shifts (the
  hand coupling clips spans at shifts), so span derivation upstream of FHPs would make the
  coupling unbuildable. Acyclicity holds because templates attach to markers (authored
  positions), never to derived spans, and a marker is a forced boundary in BOTH machines, so
  the FHP run head and the span front coincide by shared authored input rather than by one
  derivation feeding the other. **The scoping law (settled 2026-09-05)**: a template's influence
  inside FHP derivation is the run its marker heads — the fingering pins that run's anchor, the
  unsounded stops join that run's coverage and hold through it; contradiction is a per-onset
  test inside the run (tripped → the reference stops feeding, notes-only fallback, matching the
  invalidation law's "stops drawing"); a reference over silence feeds nothing. No span extent
  is ever consulted. The FHP run's END and the span's end may differ slightly at the tail; the
  census coupling counters are the instrument that will show whether the residue matters.
- **Import wiring**: GP note-level and chord-diagram fingerings assemble into template entries
  under derived spans (impossible hands dropped with a report) and stamp free notes otherwise;
  either way they reach this algorithm as anchor evidence (+1.5–1.9 measured where present).

## Open rulings for the user

1. Sign the 11-rule algorithm (or amend rules before the build).
2. The profiles question: accept the no-profile-UI recommendation, or overrule and pick which
   parameters surface anyway.
3. The fingering model above (grip templates referenced from markers + free-note fingers) as
   the format's fingering carrier and this algorithm's input — the single largest remaining
   accuracy lever.
4. The derived-stream proposal: delete the stored `fhps` stream and derive FHPs at read from
   notes + the authored opinion layer — three record kinds: marker template references,
   free-note fingers, and open-stretch window statements (manual FHP specification per the
   context-split language above).
5. The derived-default suggestion for unreferenced grips (flagged in the span-marker plan — it
   reverses the killed auto-match of 2026-08-31 and needs explicit re-confirmation).
