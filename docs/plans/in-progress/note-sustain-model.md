# Note sustain model — actual durations stored, presentation derived

**Status:** Implemented on master; the `note-sustain-experiment` branch is gone and the merge
question with it. The data model, the editor's Alt reveal of the actual rings, and the derived
hand-posture spans all stand. What remains open is the span-end question (item #59 — should a
span's end stop obeying the margin), the arpeggio-bracket verb's dependency on a posture
dictionary, and the converter default of ruling 7.

## The model in one paragraph

`ChartNote::sustain` is the **actual duration the string rings** — Guitar Pro's notated duration at
import, what the editor's verbs author. It is strictly positive on EVERY note, with no exception —
every note sounds, and a dead note's damped stroke has a duration too. Every surface **draws** the
stored note, and what the game will **score** is what is drawn: presentation publishes only the
**ink end** — where a surface stops drawing each ring — derived once per chart revision by
`chartPresentation` in common/core from the tail rules, a pure read-side derivation rather than
import-time destruction. Nothing moves: every keyframe keeps its stored instant. The legato
resolver reads the actual duration under strict adjacency. The span-implied hold the 3D board pins
heads for is derived from the ink ends, the tail law's verdict and the hand-shape spans. Nothing
that is drawn is stored; nothing that is stored is a guess.

## Why

Three readers need a duration the chart would otherwise no longer have: the span-end question, the
short-tail hole in the 2D lane, and MIDI playback. An importer that trims and drops destroys the
notated durations and salvages only part of the information, so every later reader that needs a
duration guesses independently — the legato hold test assumes a hold under the kept bound, the
span-implied hold invents one for tail-less strums, the span's end floors onto its own last strum,
and MIDI playback would need a fourth guess. That is the rule-stated-in-N-places defect class.
Storing the truth once moves the only unavoidable guess to where the information genuinely does not
exist — a lossy source format's import (ruling 7) — and deletes the rest.

## Two forms of a note, and one drawn length

| Form | Produced by | Read by |
|---|---|---|
| **memory** | the editor's verbs, the importer | everything below |
| **saved** (`savedChartNote`) | memory with a scrape's latent overrides stripped | the document writer, the validator, the resolver, both painters, hit testing, the future scorer |

`ChartResolutions` carries the connections (`ChartConnections`, which owns `saved_notes`), the
per-note `ink_end` and the tail law's verdict, and the per-note `holds`, computed together once per
chart revision; the projection publishes each note's stored end and ink end
(`NoteViewState::ring_end_seconds`, `ink_end_seconds`) and `display_hold_ends` from the holds.

## The presentation rules (stated once in core, not at import)

Input: the saved stream in chart order; the tempo map. Output: per input note, the ink end and the
tail law's verdict beside it. No note of its own, and nothing moved.

1. **Crop to the margin.** The *binding* onset is the first later sounding onset — a different
   grid position on any string — that the ring does not *pass*, passing meaning running *strictly
   past* it. The ink stops one minimum-sustain margin before that onset, floored onto the tick
   lattice and never before the note's own onset; a ring ending exactly *on* an onset passes
   nothing and binds there. A ring no later onset binds draws whole. **Deliberate hold:** a ring
   that passes an onset (a tie merged across a neighbour, a cross-voice hold) still earns its
   group's tails under rule 2, but does not skip this crop — leaving it whole lets a ring-through
   die on a later head with no gap at all. The stretch from the crop to the ring's end is the
   ENDING ZONE: its keyframes keep their stored instants and are neither drawn nor judged except
   under a reveal, and the leg crossing the crop is drawn on its true path and stops there. So the
   stored chart may end a fall or a bend exactly ON the next head of its own string, because the
   store holds what the hands did (user ruling, 2026-09-21).
2. **Drop short effect-free tails, per onset group.** A group that carries no sustain technique
   (bend, slide, slide-out, vibrato, tremolo) on any member, no deliberate hold, and no member whose
   *actual* ring runs LONGER than the kept-sustain bound (`g_minimum_kept_sustain_seconds`, which is
   the one place its value is stated — a duration, read in seconds through the tempo map, so the
   same written value earns a tail below a crossover tempo and drops it above) draws no tail on
   any member. Any member earning a tail keeps every member's.
3. **A dead note draws no tail** unless tremolo or a slide payload keeps it making noise or
   travelling (E25). This is a presentation rule, not a stored-field repair: the stored ring is the
   timing the legato adjacency test reads.
4. **A tail that shows no technique information RESTS** — the tail law. A verdict-only filter, run
   last: it reads the stored stream, judges, and MARKS where each tail rests
   (`ChartPresentation::rested_from`), inventing and erasing no length, so every ink end rules 1 to
   3 left standing is unchanged. The verdict is an OFFSET — the last always-visible landmark, past
   which the curtain owns the ribbon: zero for a plain ring, the last statement's offset (held to
   the ink end) for a ring that finishes stating and goes plain, and the ink end for a
   handed-over member, whose transfer finishes at the takeover. A ring still stating at its own end
   — a bend held to the end, a shake that never stops, tremolo, a slide-out's travel — rests
   nothing, because the curtain has no vocabulary for a statement in progress. THE CURTAIN IS
   UNIVERSAL: span furniture is no part of the question, so every fretting-hand tail in scope rests
   over open board exactly as under a bracket. THE ATOM IS THE MEMBER: each member is judged on its
   own. Scope binds both sides of the judgment — right-hand onsets are neither
   members nor witnesses. The 3D board suppresses the resting remainder at distance and reveals it
   near the hit line; the 2D lane draws the execution form always. The full statement, with its
   cases, lives at `chartPresentation` in `chart_presentation.h` and is not restated here.

Rules are applied in the order written; rule 2's verdict is shared per group, every other rule is
per note.

## Invariants on the stored form

- `sustain > 0` for EVERY note, with no exception — every note sounds. It is a structural
  refusal (no repair can invent a duration). A MISSING `sustain` key is a malformed document, and
  that is the format tripwire that actually fires: the pre-model writer elided the key on every
  tail-less note rather than writing a zero, so that is the path a real old package takes, and its
  message is the one that names the re-import.
- A tail never crosses the next onset on its own string (40-Q2-B). It is a rule of the one
  normalizer (`ChartRepair::OverlappingTail`), run at load, by the importer before its synthesis
  passes, and by the editor's plan gate — one authority rather than an editor-only trim. It asks
  `sustainBoundOf`, a binary search over the sorted stream, so the document reader refuses an
  out-of-order document itself rather than handing the normalizer a stream its searches cannot
  read (the validator's own order refusal comes too late — the normalizer runs first).
- Payload offsets lie within the actual ring.
- A scrape's terminal sits exactly at its actual end.

## Legato

`predecessorHoldReaches` is strict adjacency: the predecessor's **actual** ring reaches the
successor's onset. There is no kept-bound assumption and no margin slack — both were compensations
for the trimmed encoding — and the resolver reads the predecessor's stored sustain directly (no
span-extended parameter). A chug chained to its restrike (GP tiles durations) justifies its
hammer-on; a note followed by a rest does not, and the settle sweep flattens that claim at load and
reports it.

## The hold (3D pinned heads)

`holds[i]` starts at the ink end, with one exception: a HANDED-OVER member starts at its
STORED ring, because the next strike on its string takes the sound there and the same-string clamp
puts the stored ring exactly on that takeover.

That floor is then raised, per onset group, wherever a shape span COVERS the group's onset. Every
live fretting-hand member the span covers is held for the REST OF THE SPAN — while the grip is
held, the board pins what is held — and a member that RESTS is first floored at its own stored
ring, which may reach past the span, because the string genuinely rings there. THERE IS NO
STRUM-SIZE GATE: a lone covered member is a grip member exactly as a strummed one is, and in a
derived chart a lone tail-less note a span covers past was necessarily renewed, since an un-renewed
death would have broken the grip.

Four members are passed over inside a covered group. A DEAD member is choked, never held — its fate
belongs to rule 3's mute at either end, which is what chokes a wholly dead group with no unanimity
rule. The OTHER HAND's onsets are passed over because a tap is a member of nothing the grip states.
A HANDED-OVER member is excluded whole: its sound ends at its own stored ring, and the strike that
takes the string owns the display from there. And a member whose tail STANDS AND NEVER RESTS states
its own hold — its ribbon already says where its ring ends. That last test reads the tail law's
VERDICT and NOT tail emptiness: resting members keep their ink ends, so keying on an empty tail
would release the very pins the span convention exists to set.

A member no span covers — every plain note on open board, now that the curtain rests them all —
holds to its ink end.

Structurally this is the span convention unchanged: `chartHolds` IS that convention, asked of the
ink ends and the verdict. It extends the members whose tails rest as well as the ones rules 2 and
3 emptied. The span engine is private to `chart_presentation.cpp`, since nothing resolves holds from
a trimmed stored form.

**The span is the whole answer, and the note's own ring does not cap it.** The dichotomy the cap
was reaching for is right — the shape says the hand stays down, the ring says how long the string
sounds — and the cap contradicted it: the hold is the HAND's length, and a ring ends for two
reasons of which only one lifts a finger. The string stopped sounding, or the string was struck
again. THE CONTINUITY LAW ([D3]) already ends a span at the first member to stop stating its stop,
so a cap could only ever bite where a span outlived a member's ring — and a span only does that
when a later restatement carried it, which is positive evidence the hand never left. Its one live
effect was therefore in the place it was wrong: a REPEAT CHAIN. A stored chug chain is
strike-into-strike (that adjacency is what merges it into one span at all), so the cap ended the
chain's first strum's hold exactly at the second strum's onset — and every strum after it is a
repeat box, which draws no heads of its own. The held shape vanished one box into the chain, where
the whole point of the boxes is to say it is still held. The ring is what the TAIL draws; the span
is what the pinned head draws; one fact each.

The hold is the BOARD's alone. The 2D lane draws, lays out, hit-tests and culls by each note's
own ring and ink end, so a chug under a span wears a bare head there: the chord box already
states how long the posture is fretted, and repeating that as a ribbon spends the one mark that
means "this string is still ringing". The board, having no chord box, pins the heads to say the
same thing.

## What the importer keeps deciding (meaning, not presentation)

Tie merges and legato merges (the canonical long actual durations), grace leads and on-beat shifts
(sounding truth, including the before-beat steal), the scrape gesture's path, the shift-slide
arrival keyframe ON the landing's own instant (synthesis: GP states no arrival time), the slide-in
scoop window, dead notes, open-string floors. It does not trim, drop, clip payload, or ASSIGN the
arrival window to the sustain. Synthesis may still GROW a ring too short to carry the keyframe it just
fabricated — payload has to lie inside the ring, and the note sounds while it travels — but never
shorten one; growing to the landing instead would be inventing a ring the source never notated, and
would turn every such origin into a rule-1 deliberate hold wherever another string sounds inside
the gap. It runs the same-string clamp after every pass that can lengthen a ring (a re-strike stops
the ring), and hand-placement generation at trail-off ends (`resolveSlideOutExits`) reads the
stored ring it settles, so a trail-off's hand exit lands where the gesture really ends.
Hand-posture spans are not an import decision: they are derived wherever they are read.

## Accepted deviations from what the old import policy drew

Each of these is a consequence of deriving presentation from the stored ring, and each is accepted:

- A grace note's own lead-length tail (≈60 ms) does not draw: its principal binds it at the
  sounding position, which the notated grouping used to exempt. Its hold goes with it.
- An on-beat grace that delays only some members of a strum leaves the other members drawn in
  full rather than margin-cropped (their ring strictly passes the fabricated onset).
- Claims after a rest flatten under strict adjacency.
- The 2D lane draws no derived sub-quarter hold ribbons; the lane does not read the hold at all.

Two more come from the same root — the presentation rules partition and bind on the SOUNDING
position because the saved form is all they have, where the import policy read each event's NOTATED beat:

- **A ring ending on a graced beat is an importer defect, not a rule-1 case.** A before-beat grace
  fabricates an onset one lead (a 32nd) ahead of its principal, and an importer that left the
  preceding beat's notes ringing to the principal stored a ring overlapping the grace, which is not
  what the score sounds like: Guitar Pro plays a before-beat grace by stealing its lead from the
  preceding beat. The fix is the importer's — the notes of the voice's preceding beat end at the
  run's first onset. The predecessor then ends exactly on the grace's onset, rule 1 crops it to the
  margin as the import policy did, and a tied predecessor still merges past the principal (the tie
  merge keys on the tie flag, not adjacency) and draws as the genuine hold it is. Rule 1 keeps
  its wording; a margin-wide overrun threshold was considered as an alternative and declined — a
  heuristic over a wrong datum.
- **Rule 2's tail verdict is per sounding position.** The import policy keyed the verdict by
  notated beat, which put a grace and its principal's whole strum in one bucket. Now a before-beat
  grace carrying a slide does not earn its chord's tails (the chord drops them, the grace keeps its
  own), and an on-beat grace splits the delayed member out of its strum. Both are the sounding
  grouping, which is the only one the stored form has, and the grace's own head separates it from
  the chord on both surfaces.

And one from rule 1's own reading:

- **A tie-merged shift-slide origin crossing another string's onset is bound by its landing, not
  by the onset it crosses.** Its stored ring runs strictly past the intervening onset on another
  string, so that onset does not bind it; the landing that re-picks it does, since the ring ends
  exactly there. The ink stops one margin before the landing, and the leg toward the arrival —
  which keeps its stored instant at the landing, as every keyframe does — is drawn on its true path
  as far as the crop. The same rule as any cross-voice hold.

**Fret-hand positions and span geometry are unaffected by the model**, and the strict hold test
changes no resolved legato motion on the local corpus: Guitar Pro tiles durations, so an imported
claim's predecessor rings exactly to its onset, and the kept-bound assumption it replaced was never
load-bearing there. The accepted "claims after a rest flatten" deviation is real by construction
and has no instances on that material.

## Stages

- **A** — the data model: A1 core (`chart_presentation.h/.cpp`, the hold, the validator and
  document rules, tests); A2 importer (emit actual, no trimming, tests migrated); A3 readers
  (projection on the stored notes and their ink ends, `display_hold_ends` on holds, the shared
  arrival rule `chartShapeArrivals` on the stored stream, resolver strict); A4 editor verbs (growth
  clamps at adjacency, shrink refuses zero, insert default one grid step, assist to the onset);
  **D1** — the 2D lane draws each note to its ink end (`NoteViewState::ink_end_seconds`), the hold
  parameters are absent from the paint core and the layout
  manifest, and `display_hold_ends` is the 3D board's field alone. That retires the watch item on
  the two surfaces' holds diverging: with no 2D hold there is nothing left to diverge.
- **B — the editor's Alt reveal, on the real-tails mark.** While Alt is held the 2D lane draws
  every note on to its ring end — the ring the string really sounds for, techniques riding it and
  the ending zone's keyframes shown at their stored instants; release stops every note at its ink
  end again. The mark was chosen over a hairline outline drawn on top of the cropped notation: the
  ring should simply BE the notation. Since `ring-ends-and-authoring-planes.md` it needs no second
  projection: every note carries both ends, so the reveal is a per-index answer to how far a paint
  reads, the lane culls on the ring ends, and the foreground-and-Alt predicate below drives it.

  **Tracking.** The reveal is on exactly while `juce::Process::isForegroundProcess() &&
  juce::ComponentPeer::getCurrentModifiersRealtime().isAltDown()` — "the app" is the PROCESS, so
  the editor window or the 3D preview being active both count — read from ONE place, `EditorView`'s
  per-frame vblank attachment (the one that already samples the meters and the time readout, for
  the view's whole life), and from nothing event-driven. The requirement is that Alt work if and
  only if the app is in focus, and not change with where the mouse is in the app or which window
  in the app the mouse is over. That is not the axis JUCE's modifier callbacks are keyed on —
  `modifierKeysChanged` goes to the component under the pointer (else the focused one) and up THAT
  window's chain, and a release mid-Alt+Tab goes to another application — so an event-driven
  sampler keyed on pointer position, a deep mouse listener, a preview forwarding hook, and a
  reveal timer are all the wrong shape and none of them exists. Both halves of the predicate are
  process-wide OS queries, so the per-frame sampler is the only one that cannot be wrong about
  where the pointer is. A gate on the editor WINDOW's focus is likewise wrong, and for a precise
  reason: the preview grabs focus, which is exactly why the test is the process.

  - **Drawn = judged stays structural.** The board, the game and the scorer draw and judge to the
    ink end; only the 2D lane reveals. Hit testing and the marquee reach a keyframe past its ink
    end only while its note is revealed, exactly as it is drawn, and no tail is ever a target.
  - **One glyph consequence of drawing past every rule, accepted.** A DEAD note grows a tail under
    the reveal (rule 3 is a presentation rule) — Alt shows what is STORED, and that tail reads as
    how long the mute is held.
  - **The modifier and the selection reveal** (`chartNoteRevealed`, re-ruled 2026-09-23 once a
    reveal moved no mark): Alt is the insertion lookahead the selection cannot serve, since with a
    selection standing a typed digit RETYPES those notes instead of inserting one, and the
    selection reveals the thing under scrutiny so a selected zone keyframe always has a ring.
- **C — shape spans and their postures are DERIVED from the notes**, per chart revision, in core:
  `deriveChartShapes(saved_notes, claimed_stops, planted_stops, tempo_map)` (`chart/chart_shapes.h`)
  reads the stored stream and the resolved claim tables, and `ChartResolutions` carries `shapes` and
  `postures` beside the ink ends. The shared arrival rule `chartShapeArrivals` lives in the
  same header: with the span rules gone from the validator it was the last thing making
  `chart_rules` know spans exist, and both halves of one derivation belong in one file. Item #59
  (should a span's end stop obeying the margin) is untouched and still open. The derivation's own
  law — when a span opens, runs and ends — is grip tenure, and
  `docs/plans/in-progress/span-derivation-ground-up.md` is its record.

  **`templates` is not in the format**, and neither are the spans: nothing authored a posture, so
  the posture is `ChartPosture` with no `name` and no `fingers`, and every consumer branch that
  could only ever read an empty one is gone — `ShapeViewState::name` and the highway's chord-name
  text pass, `ShapeStringViewState::finger` and the highway's fingering panel (with its
  `fingering.png` asset and `HighwayTexture` enumerator), and the timeline ruler's name-chip band.
  When names and fingerings are ever authored they arrive as a dictionary keyed by a posture, not
  as fields on one; the display question is SETTLED in
  `docs/plans/in-progress/arpeggio-posture-display-options.md`, but two of the options it rejected
  would need that dictionary before they could ever be revisited. The reader refuses a
  document carrying either key, with the re-import remedy named — the same tripwire the removed
  note fields get. The validator has no posture or span rules (`normalizeChordTemplate`,
  `ChartErrorCode::InvalidTemplate` and `InvalidShape` do not exist): derived data cannot be
  invalid.

  **Deriving at read time fixes a class the importer got wrong.** The importer derived spans BEFORE
  `normalizeChart`, so it could describe notes the chart does not contain — the traced case is
  three consecutive all-open dead strums whose middle strum carried a slide on an open string,
  which the normalizer then dropped, splitting one span into three. Derived at read time the rule
  sees the settled stream and merges them, which is what the chart says.

  Two structural constraints the derivation carries, neither of them visible in output. Running on
  the load path puts it BEFORE `validateChartRules` — against a document whose `"string"` values
  nothing has bounded yet — so the posture array's width is `g_max_chart_strings`, a constant
  rather than a quantity read off the input (a `"string"` of two billion asks for 17 GB and kills
  the load with the refusal it was about to earn); the tuning would have been no safer, since it is
  equally unvalidated at that point. And `ChartConnections`/`chartConnections` are separate from
  `ChartResolutions`: `resolveLegato` reads the saved stream alone, so the settle sweep and the `H`
  verb — which run at every caret move, seek and selection change, and read nothing else — would
  otherwise derive the whole song's ink ends, spans and holds on every keystroke and throw
  all three away. `ChartResolutions` carries the connections rather than restating them.
- **D — the highway carries no mark for a short note's length.** The candidate was a floor mark
  under each marked note running from its onset to the ring's end, cyclable through filled,
  outlined and light forms — the light being a mild lighting effect on the floor, in the colour of
  the note, like the lighting already there. **The ruling is that none of them earns a place: any
  mark on the highway for a short note's length adds clutter, not value.** So there is no
  diagnostics rig on the renderer — no `ActualRingLook`, no `HighwayDiagnosticsOptions`, no
  `HighwayRenderer::setDiagnosticsOptions` or preview pass-throughs, no ring-mark draw pass, and no
  commands for one (ids 0x130B, 0x130D and 0x130E stay retired). The 2D lane carries the same datum
  under `Alt` (stage B): it has the space for a length that the board, read at speed, does not.

  What the shipped board reads, and why these helpers exist on their own terms:
  `highway_slide_path.h` (`highwayNoteFretboardX`, `highwaySlideStateAt`, `highwayGlideSliceCount`)
  and `highway_floor_geometry.h` (`highwayVisibleSpan` and `HighwaySpan`, `highwayFloorFootprint`
  with `g_open_tail_margin`). The sustain tail, the head, the hand-window light and the tap light
  ask all of them. `HighwaySpan` carries `from` and `to` and no `ends_inside`, which only a band's
  end cap and a floor light's far fade would read.

  There are **two visual paths**, not three: `docs/developer/the-3d-highway.md` records in one
  paragraph what a world-space mark would need if the shape is ever wanted again (the overlay path
  cannot express one, and its switch must never be a `HighwayDisplayOptions` field).

## Rulings recorded

1. Held = strict adjacency.
2. No back-compat: every existing chart is re-imported.
3. The 2D hold ribbons change lands as its own commit after A.
4. What is drawn is what is judged, a contract recorded here and exposed through the projection
   (`NoteViewState::ink_end_seconds`); the scorer reads it when it exists.
5. Every note's sustain is positive; a dead note carries its notated duration and E25 is a
   presentation rule (the "except dead" wording was serving the same end, and pinning dead at zero
   would re-break every legato claim after a muted cluck). A quarter-note cap on a dead note's
   stored sustain was considered and declined: nothing visible or audible changes with the length
   (rule 3 draws no tail, playback clucks), the length is exactly the timing information and the
   legato adjacency the stored form exists to carry, and the cap would be one more bound stated in
   three places for a value no surface shows. The same-string clamp is the only bound on any note's
   sustain, and no note stores a zero.
6. Insert default = one grid step, clamped at the next onset on the string. Rule 12 unchanged.
7. **Proposed, awaiting the user's confirmation — the lossy-source default.** A converted package
   from the commercial source format stores no duration for a note the charter did not mark as
   held. The plan puts the one unavoidable guess at that import, in the external converter: *a
   source note with no sustain rings to the next onset on ANY string, capped at half the
   kept-sustain bound; a note with a sustain rings for it.* Any string, not its own: rule 1
   draws a ring running strictly past the first binding onset in full, so a chug defaulting to
   its own string's re-strike would draw through every alternating-string riff. Capped below the
   kept bound: a default running longer than it would earn a tail under rule 2 that the source never
   showed. Consequence to accept with eyes open: the source's holds inside the bound stop drawing,
   because
   presentation is one rule for every chart where the package path used to skip the Guitar Pro
   rules. Folds into the stale-package re-export (task #78). No accent exception: charters of that
   format often read an accent as staccato, but the stored ring of an accented note inside the
   bound changes nothing drawn (the accent glow is the staccato read), a shorter default would
   flatten a hammer-on the charter marked after it, and playback can honour the accent itself when
   it exists rather than the duration storing a convention.
8. **The duration verb is a GESTURE.** A run of steps — each against the placement quantum's
   lattice as it stood when that step was made, so a run may span a snap toggle or a grid change —
   is recorded IN ORDER, and every selected note is recomputed by REPLAYING that run over its
   PRE-GESTURE ring: the answer clamped up at its own string's `sustainBoundOf`, and holding the
   ring it currently has when the replay is not positive (there is no empty ring; it rejoins the
   moment the replay is positive again). The reason is symmetry: every note replays the same steps
   from where it started, so whatever shape the selection's tails had comes back intact and nothing
   blocks anything else — a chord member pinned at its bound on the way out rejoins its neighbours
   exactly where it left them, where per-step clamping would bake the clamp into the next step's
   starting value and shrink the chord asymmetrically. Neither bound enters the replay, which is
   what preserves that: they judge its answer. The run is also ONE undo entry rather than one per
   keypress. The first step pushes it, every later one replaces it (`replaceTop`), so the entry
   always describes start → now, and the pre-gesture values need no snapshot because that entry
   reversed IS the pre-gesture stream (the settle fold's method). A run whose delta nets back to
   zero has nothing left to describe, so its entry is DROPPED and the chart walked back (`dropTop`,
   the technique toggle's own ending): an entry describing nothing is a dead Ctrl+Z on a document
   reported modified that is byte-identical to the saved file. The gesture is live under the
   technique toggle's own proofs — the selection unchanged, and the burst record still owning the
   history top — and ends wherever the toggle window ends (selection change, caret move, any other
   edit, undo/redo, a committing settle, a save); the next step then opens a fresh gesture from the
   current rings. The chart verbs share ONE window field (`m_chart_verb_window`, a variant),
   because at most one can ever be armed.

   Off-grid authoring is a session MODE (`Ctrl+G`) behind one **placement quantum** — the grid note
   value while snap is on, the 1/3840-whole-note tick while it is off — read by every verb that
   quantizes a time POSITION; there is no `Ctrl` fine tier. The design is
   `docs/plans/completed/grid-snap.md`.

   **Addendum — a step moves the ring's END, so the gesture keeps its STEPS.** A summed delta is
   wrong about what a step IS: leave a tail's end between lines — snap-off placement can — and
   every later grid step carries the remainder forever, so the tail never returns to the grid the
   user is looking at. A step moves a POSITION, and a position step has no size of its own:

   - A step moves the ring's END — the onset plus the ring, an absolute grid position — to the
     adjacent line of the placement quantum's lattice strictly beyond it in the step's direction,
     through the ONE step primitive the caret and the lane nudge already walk with
     (`adjacentTempoGridPosition`). From an end already on that lattice it is exactly one step;
     from an end between lines it snaps, ceiling when growing and flooring when shrinking. No
     snapping rule is restated in the planner, and the step carries the NOTE VALUE rather than a
     beat amount so the meter at whatever measure the end lands in scales it (a quarter-note step
     is one beat in x/4 and two in x/8).
   - With snap off that lattice is the tick, so a step is a 1/3840-whole-note nudge, reached by the
     mode rather than by a modifier.

   `ChartSustainGesture` therefore holds the step list, not a delta; `planAdjustSustain` takes it
   and replays it per note. Consequences, all intended and pinned by tests:

   - A gesture whose steps all share one lattice, from a ring already ON that lattice, is exactly
     reversible: each step lands where its opposite steps back through, and the bounds stay out of
     the replay.
   - A step from a ring sitting BETWEEN that lattice's lines snaps by design, so reversing it lands
     on the line BELOW, not on the ring the gesture started from. A step means "put the end on the
     line"; a remainder surviving it is the bug.
   - A chord whose members sit at different offsets snaps each member to its OWN next line, because
     the replay runs per note from that note's own end.
   - A run that replays every note back to `base` is NoChange, so the entry is retired. The undo
     label names the entry's NET direction — the total change the replay makes to the selection's
     rings, start → now — because the entry describes the whole gesture: grow, grow, shrink is a
     growth of one step and its undo shortens the ring, so a label read off the last press
     ("Shrink") would lie about what Ctrl+Z does. The steps themselves have no sign to sum; the
     entry's own change does.
   - The reversibility claim rests on a primitive that has to be an involution, and the fault it
     once had was older than the gesture: an `adjacentTempoGridPosition` that stepped one grid step
     and re-snapped to the NEAREST line skipped a measure's last line whenever it sat exactly half
     a step before the next downbeat (a 1/4 grid in 7/8: back two beats from the downbeat lands
     halfway between beats 5 and 7, and the tie-to-earlier rule picks 5), so a grow-then-shrink
     took a six-beat ring to four. The caret step and the lane nudge shared it. The adjacent line
     is read off the lattice directly (`common::core::adjacentGridPosition`, beside
     `snapGridPosition` on one lattice helper), the editor primitive delegates to it, and the walk
     is an involution on every meter — pinned in both suites by walking the lattice forward and
     back.
   - **The shape is no longer this verb's alone.** The `Alt`+arrow MOVE burst now runs it too, over
     the objects a run started on rather than the rings, through ONE authority both verbs call
     (`commitChartGestureStep`): the pre-gesture reconstruction, push-or-replace, retire-on-NoChange
     and window arming are shared, and each verb writes only what a STEP means. A move step keeps
     the list for the reason above one axis over — the quantum is scaled by the meter where the run
     has REACHED, so a run crossing a signature change steps by two different amounts — and it is
     the one that must also state where the run has LANDED, since a note's key is its slot and a
     keyframe's identity is its offset, so every step re-points the selection the window proves
     against.
9. Retired with the mark it governed: the reveal's outline was to draw in
   `EditorTheme::lane_overlay` at half alpha rather than in a dimmed string colour, and the outline
   is not the mark that shipped. The number is kept so earlier references stay readable, and the
   reason that outlives it is general: the lane's quieting authority (`Ink` leaned toward the lane
   ground by `ghosted`) is the NOTATION's and is private to the paint core, so editor chrome may
   never restate it or borrow the notation palette. The mark that ships needs no ink of its own —
   it IS the notation.
