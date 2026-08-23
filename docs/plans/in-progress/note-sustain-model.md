# Note sustain model — actual durations stored, presentation derived

**Status:** In progress on branch `note-sustain-experiment`. Stage A built 2026-08-21 (A1–A3 plus
D1); stages B, C and D built 2026-08-22. Every stage now awaits the sighting — D most of all,
since it is a rig for making the other decisions rather than a decision itself.

**Authored** 2026-08-21 out of the span-end discussion (review item 13), the short-tail hole the
user found in the 2D lane, and the MIDI-playback requirement.

## The model in one paragraph

`ChartNote::sustain` is the **actual duration the string rings** — Guitar Pro's notated duration at
import, what the editor's verbs author. It is strictly positive for every note (a dead note's
damped stroke has a duration too). What a surface **draws** and what the game will **score** is the
**presented** form, derived once per chart revision by `presentedChartNotes` in common/core from
the import policy's three tail rules, which stop being import-time destruction and become a pure
read-side derivation. The legato resolver reads the actual duration under strict adjacency. The
span-implied hold the 3D board pins heads for is derived from the presented and actual forms
together. Nothing that is drawn is stored; nothing that is stored is a guess.

## Why

Today the importer destroys the notated durations (rules 1–3 trim and drop), salvages part of the
information into the stored shape span (rule 11 reads the *pre-trim* ends), and every later reader
that needs a duration the chart no longer has guesses independently: the legato hold test assumes
a hold under the kept bound, the span-implied hold invents one for tail-less strums, the span's end
floors onto its own last strum, and MIDI playback would need a fourth guess. That is the
rule-stated-in-N-places defect class. Storing the truth once moves the only unavoidable guess to
where the information genuinely does not exist — a lossy source format's import — and deletes
the rest.

## Three forms of a note

| Form | Produced by | Read by |
|---|---|---|
| **memory** | the editor's verbs, the importer | everything below |
| **saved** (`savedChartNote`) | memory with a scrape's latent overrides stripped | the document writer, the validator, the resolver |
| **presented** (`presentedChartNotes`) | saved notes through the tail rules | both painters, hit testing, the future scorer |

`ChartResolutions` carries the connections (`ChartConnections`, which owns `saved_notes`),
`presented_notes`, and the per-note `holds`, computed together once per chart revision; the
projection builds `NoteViewState` from the presented note and `display_hold_ends` from the holds.

## The presentation rules (moved from import, stated once in core)

Input: the saved stream in chart order; the tempo map. Output: one presented note per input note.

1. **Trim to the margin.** The next *binding* onset is the first later note at a different grid
   position on any string. The presented tail ends at least one minimum-sustain margin (at the
   note's own measure) before it. **Deliberate hold**: a ring that runs *strictly past* that first
   binding onset is presented in full, however many later onsets it crosses (a tie merged across a
   neighbour, a cross-voice hold).
2. **Payload floors the trim.** The tail extends to the last payload point that *changes* something
   — a bend point differing from its predecessor, a waypoint differing from the previous fret — and
   stops exactly there; trailing non-changing points leave with the tail (clipped in the presented
   note, never rescaled: GP's bend curve is anchored to the notated ring). A slide-out is not
   protected payload: its presented terminal compresses back with the tail, floored at the minimum
   slide window and kept strictly after the last surviving waypoint. A scrape's terminal is the
   gesture's end and compresses by the leg rule: a leg starting before the margin line ends on it;
   one starting on or after it halves its distance to the onset.
3. **Drop short effect-free tails, per onset group.** A group that carries no sustain technique
   (bend, slide, slide-out, vibrato, tremolo) on any member, no deliberate hold, and no member whose
   *actual* ring reaches the kept-sustain bound (a quarter note) presents no tail on any member.
   Any member earning a tail keeps every member's.
4. **A dead note presents no tail** unless tremolo or a slide payload keeps it making noise or
   travelling (E25, unchanged in substance, now a presentation rule rather than a stored-field
   repair).

Rules are applied in the order written; rule 3's verdict is shared per group, every other rule is
per note.

## Invariants on the stored form

- `sustain > 0` for every note. Structural refusal (no repair can invent a duration); a missing
  `sustain` key is a malformed document. The MISSING-key refusal is the format tripwire that
  actually fires: the pre-model writer elided the key on every tail-less note rather than writing
  a zero, so that is the path a real old package takes, and its message is the one that has to
  name the re-import (corrected 2026-08-22 — the positive-sustain rule states the same remedy for
  a zero that no writer ever emitted).
- A tail never crosses the next onset on its own string (40-Q2-B). Now a rule of the one
  normalizer (`ChartRepair::OverlappingTail`), run at load, by the importer before its synthesis
  passes, and by the editor's plan gate — one authority instead of an editor-only trim. It asks
  `sustainBoundOf`, a binary search over the sorted stream, so the document reader now refuses an
  out-of-order document itself rather than handing the normalizer a stream its searches cannot
  read (the validator's own order refusal comes too late — the normalizer runs first).
- Payload offsets lie within the actual ring (unchanged; the meaning widens).
- A scrape's terminal sits exactly at its actual end (unchanged).

## Legato

`predecessorHoldReaches` is strict adjacency: the predecessor's **actual** ring reaches the
successor's onset. The kept-bound assumption and the margin slack — both compensations for the
trimmed encoding — are deleted, and the resolver reads the predecessor's stored sustain directly
(no span-extended parameter). A chug chained to its restrike (GP tiles durations) justifies its
hammer-on exactly as before; a note followed by a rest no longer does, and the settle sweep
flattens that claim at load and reports it.

## The hold (3D pinned heads)

`holds[i]` is the presented end, except that a member of a 2+ onset group under a covering
shape span whose presented tail is empty holds for its actual ring, capped at the span's end. An
all-dead group is choked, as today. Singles hold for their presented tail. The same-string bound
needs no cap of its own: `normalizeSustainOverlaps` already holds every stored ring inside its own
string's next onset, so capping at the ring caps at the bound too — the A3 review deleted the
second statement of it that had been sitting inside the span engine, unable to do anything but
agree with the first.

Structurally this is today's span rule unchanged: `chartHolds` *asks* the span convention —
handing it the presented stream, so it extends exactly the members presentation emptied — and caps
each answer at the stored ring. The rule is composed over, never restated; the span engine is
private to `chart_presentation.cpp` now that nothing resolves holds from a trimmed stored form.

The cap is a real change of value, though, not a rename: today's hold is the span's remainder
whatever the strum rang for, so a chugged chord under a long shape shortens from the span's end to
its own eighth, and the 3D board pins its heads for that much less. That is the intended reading —
the shape says the hand stays down, the ring says how long the string sounds — but it must be
counted in the stage-A report rather than described as a no-op.

Since D1 the hold is the BOARD's alone. The 2D lane draws, lays out, hit-tests and culls by each
note's presented tail, so a chug under a span wears a bare head there: the chord box already states
how long the posture is fretted, and repeating that as a ribbon spent the one mark that means "this
string is still ringing". The board, having no chord box, pins the heads to say the same thing.

## What the importer keeps deciding (meaning, not presentation)

Tie merges and legato merges (the canonical long actual durations), grace leads and on-beat shifts
(sounding truth, including the before-beat steal), the scrape gesture's path, the shift-slide
arrival waypoint at `gap − margin` (synthesis: GP states no arrival time), the slide-in scoop
window, dead notes, open-string floors. It stops trimming, dropping, clipping payload, and
ASSIGNING the arrival window to the sustain. Synthesis may still GROW a ring too short to carry the
waypoint it just fabricated — payload has to lie inside the ring, and the note sounds while it
travels — but never shorten one; growing to the landing instead would be inventing a ring the
source never notated, and would turn every such origin into a rule-1 deliberate hold wherever
another string sounds inside the gap. It runs the same-string clamp after every pass that can
lengthen a ring (a re-strike stops the ring) and feeds its two presentation-riding passes —
hand-placement generation at trail-off ends, and shape-span derivation — the presented notes, so
their outputs do not change in stage A.

## Accepted deviations from pixel identity, measured

A golden harness (local only, corpus firewall) dumps every note of every local GP score in the
pre-change saved form and in the post-change presented form; the diff is the proof. Known, accepted
residuals, each counted in the stage-A report:

- A grace note's own lead-length tail (≈60 ms) no longer presents: its principal binds it at the
  sounding position, which the notated grouping used to exempt. Its hold goes with it (9 notes
  measured at A2).
- An on-beat grace that delays only some members of a strum leaves the other members presented in
  full rather than margin-trimmed (their ring strictly passes the fabricated onset).
- Claims after a rest flatten under strict adjacency.
- The 2D lane's derived sub-quarter hold ribbons disappear (D1's commit; the lane stops reading the
  hold at all), and a chug's 3D pinned head shortens from its shape span's end to its own ring
  (stage A) — the one intended visible change, on both surfaces.

Two more the A1 review measured, both from the same root — the presented rules partition and bind
on the SOUNDING position because the saved form is all they have, where the import policy read each
event's NOTATED beat. Ruled 2026-08-21:

- **A ring ending on a graced beat is an importer defect, not a rule-1 case.** A before-beat grace
  fabricates an onset one lead (a 32nd) ahead of its principal, and the importer left the preceding
  beat's notes ringing to the principal — so the stored ring overlapped the grace, which is not
  what the score sounds like: Guitar Pro plays a before-beat grace by stealing its lead from the
  preceding beat. The fix is the importer's, in stage A2: the notes of the voice's preceding beat
  end at the run's first onset. The predecessor then ends exactly on the grace's onset, rule 1
  trims it to the margin as the import policy did, and a tied predecessor still merges past the
  principal (the tie merge keys on the tie flag, not adjacency) and presents as the genuine hold
  it is. Rule 1 keeps its wording; the margin-wide overrun threshold considered as an alternative
  was a heuristic over a wrong datum and is declined.
- **Rule 3's tail verdict is per sounding position, accepted.** The import policy keyed the verdict
  by notated beat, which put a grace and its principal's whole strum in one bucket. Now a
  before-beat grace carrying a slide no longer earns its chord's tails (the chord drops them, the
  grace keeps its own), and an on-beat grace splits the delayed member out of its strum. Both are
  the sounding grouping, which is the only one the stored form has, and the grace's own head
  separates it from the chord on both surfaces; counted in the stage-A report.

Two more the A2 golden diff measured, ruled 2026-08-22:

- **A quarter note before a graced beat loses its tail (57 notes in 5 of 113 songs).** The grace
  steal leaves it ringing 7/8 of a beat, under rule 3's kept bound, where the import policy read
  the notated quarter and kept a 5/8 tail. Accepted: the ring is the truth and rule 3 is doing what
  it states; dropping the steal reintroduces the ring-through-the-grace defect and exempting stolen
  leads from the bound states the rule twice. The cost is BOTH surfaces, not 2D tail ink alone: 19
  of the 57 sit under no covering shape span, so nothing re-extends them and the 3D pinned head
  goes with the tail; the other 38 keep their span's hold. Flagged for the sighting: a quarter
  before a grace with no tail beside quarters that keep theirs may read as an error in a melodic
  line, and on the highway 19 of them stop being pinned at all.
- **A tie-merged shift-slide origin crossing another string's onset presents to its landing (2
  notes in 1 song).** Its stored ring reaches the landing that re-picks it; that ring runs strictly
  past an intervening onset on another string, so rule 1 presents it whole and the tail runs the
  last margin into the landing's head instead of stopping at the arrival waypoint (the waypoint
  itself is unmoved). The same rule as any cross-voice hold; accepted.

The rest of the A2 diff, for the record (113 songs, 245,866 notes, 22,218 spans):

- **Fret-hand positions are byte-identical** in every arrangement of every song, and **span
  geometry** — every span's position and sustain — is byte-identical too.
- **Three chord TEMPLATES merged**, in one song, in each of its three arrangements (54→51, 53→50,
  48→45 distinct postures), so four to nine spans per arrangement now draw a chord box with one
  string FEWER. The cause is the grace steal, not a grown ring: a note the steal ends exactly on
  an ornament's onset is no longer ringing THROUGH it (the ring-through test is strict), so its
  string leaves the posture and that posture becomes equal to a neighbouring one. Traced case: a
  legato-attacked open string ending exactly on a before-beat grace's onset used to fold fret 0
  into that grace's posture, which is what made it distinct from the identical two-string posture
  on the next beat. Accepted for the same reason the steal is — the string genuinely stopped
  sounding there.
- **3,156 holds change in 51 songs.** 2,775 shorten and 371 end — the intended A1 hold cap, the
  fourth deviation above, plus the 342-note defect below — and 10 lengthen. The 10 are two
  different things, measured note by note: **2** are the tie-merged shift-slide origin above
  (its sus lengthened and its hold followed), and **8** are the sus additions further down — the
  same notes, their holds following the tails the sounding-position regrouping gave them.
- **342 of those hold changes are a defect, not a deviation.** A DEAD note inside a live chord
  under a covering span loses its pinned head entirely, because `normalizeChart` still applies
  E25 to the STORED ring (7,835 notes ship with `sustain` zero, and every one of them is dead)
  and the hold caps at that zero. That contradicts ruling 5 and the positive-sustain invariant.
  A3 moves E25 out of `normalizeChartNote`; this residual is to be RE-MEASURED there, not signed
  off here.
- **One sus drop, in one song, is unexplained.** An open string storing a half-beat ring, alone at
  its position, which the import policy gave a quarter-beat tail. Under the presented rules the
  drop is correct by inspection (a sub-quarter ring, no technique, no partner and no hold), and
  neither the clamp nor the grace machinery is involved: that arrangement contains no grace at
  all, and the no-clamp control dump is byte-identical to the shipped one. What earned the note a
  tail under the notated-strum key could not be reconstructed from the dumps.

### Re-measured at A3 (2026-08-22)

Against the A2 dump, the A3 readers change **exactly one thing across 245,866 notes in 113 songs**:
the 342 dead-note holds above. Every one is a dead note, every one still presents no tail (rule 4
doing its work on the presented form alone), 317 now match the pre-change hold exactly and 25 stay
shorter — capped by their own ring rather than the span's end, which is the intended A1 hold cap.
The defect is closed.

Against the pre-change baseline the whole A3 picture is therefore A2's table minus that defect:
2,839 hold changes in 49 songs (2,800 shorter, 29 ended, 10 lengthened) where A2 had 3,156 in 51
(2,775 / 371 / 10 — the 371 being 29 genuine ends plus the 342 defect). The 67 sus drops, the 8 sus
additions (a strum's longer-ringing member gaining the tail the notated-beat grouping suppressed —
the sounding-position regrouping ruled on 2026-08-21, in its other direction), the 2 shift-slide
SUS lengthenings, the 3 merged chord templates and the byte-identical fret-hand placements are all
unchanged from A2. The 10 hold lengthenings are those two groups' holds following those two tail
changes — 2 shift-slide, 8 sus additions — not 10 shift-slides.

**The strict hold test changed nothing measurable.** Not one resolved legato motion differs, from
either baseline: Guitar Pro tiles durations, so an imported claim's predecessor rings exactly to its
onset, and the kept-bound assumption it replaced was never load-bearing on this corpus. The
accepted "claims after a rest flatten" deviation is real by construction and has zero instances
here — which also retires the recorded worry that a muted-tail trim would flatten claims
corpus-wide.

## Stages

- **A** — the data model, pixel-identical: A1 core (`chart_presentation.h/.cpp`, the hold, the
  validator and document rules, tests); A2 importer (emit actual, trimming deleted, tests
  migrated); A3 readers (projection on presented, `display_hold_ends` on holds, the shared arrival
  rule `chartShapeArrivals` on presented as well, resolver strict; golden diff); A4 editor verbs
  (growth clamps at adjacency, shrink refuses zero, insert default one grid step, assist to the
  onset); **D1 done 2026-08-22** — the 2D lane draws, lays out, hit-tests and culls by each note's
  presented tail (`NoteViewState::end_seconds`), the hold parameters are deleted from the paint
  core and the layout manifest, and `display_hold_ends` becomes the 3D board's field alone. It
  retired the watch item on the two surfaces' holds diverging: with no 2D hold there is nothing
  left to diverge.
- **B — done 2026-08-22.** The editor's Alt reveal: while Alt is held every visible note in the 2D
  lane outlines its actual ring at tail height; release snaps back to the presented form. The ring
  rides the projection as `ChartViewState::actual_end_seconds`, derived beside `display_hold_ends`
  from the SAVED notes — the editor's field alone, because scored is the presented form (ruling 4),
  and `NoteViewState` deliberately does not gain it. The outline is editor furniture drawn after
  the shared paint core, like the selection ring, and reuses `tabNoteLayout`'s tail span so it
  traces where a tail of that length would sit. `paintTabLane`'s visible-span rule moved out to the
  exported `tabVisibleSpan` so the reveal culls through the same widened clip the notation does,
  against its own prefix maximum over the actual ends. **Tracking ruled 2026-08-23, replacing the
  event-driven samplers of 2026-08-22/23.** The reveal is on exactly while `juce::Process::
  isForegroundProcess() && juce::ComponentPeer::getCurrentModifiersRealtime().isAltDown()` —
  "the app" is the PROCESS, so the editor window or the 3D preview being active both count — read
  from ONE place, `EditorView`'s per-frame vblank attachment (the one that already samples the
  meters and the time readout, for the view's whole life), and from nothing event-driven. The
  user's reason, after sighting the previous design: with the
  2D view focused, Alt toggled the tails only while the pointer was over the 2D view and stopped
  when it moved over the 3D view; clicking the 3D view into focus made Alt work again; and the
  reveal sometimes flipped with the app completely out of focus. "Alt should work IFF the app is
  in focus, and not change based on where the mouse is in the app (or which window in the app the
  mouse is over)." That is the axis JUCE's modifier callbacks are keyed on — `modifierKeysChanged`
  goes to the component under the pointer (else the focused one) and up THAT window's chain, and a
  release mid-Alt+Tab goes to another application — so the `modifierKeysChanged` and
  `focusOfChildComponentChanged` overrides, the deep mouse listener, the preview's forwarding hook,
  the reveal's own timer, and its start/stop logic were all deleted together. Both halves of the
  predicate are process-wide OS queries, so the per-frame sampler is the only one that cannot be
  wrong about where the pointer is. (The earlier rejection of a gate on the editor WINDOW's focus stands for a different
  reason than it was given: the preview grabbing focus is exactly why the test is the process.)

  **The second look, behind a temporary toggle (2026-08-22).** The outline was one answer to "how
  should the ring show"; the other is that the ring should simply BE the notation. So the lane now
  draws, while Alt is held, the whole chart in a second projected form — every note's tail is its
  real ring, techniques riding it, payload restored — and the outline path stays beside it so the
  two can be flipped between. `F6` (`EditorCommandId::ToggleActualRingRevealStyle`, 0x130C, View)
  is that flip, registry-routed like stage D's F1 and ticked in the View menu; **the default is
  Tails, the thing being sighted**. Both the command and `ActualRingRevealStyle` are TEMPORARY:
  the sighting picks one mark and the loser is deleted with everything only it needed.

  - **One producer, a form parameter.** `makeChartViewState(arrangement, tempo_map, form)` with
    `ChartNoteForm{Presented, Actual}`. The form selects ONE source reference for the per-note view
    fields inside the note loop and nothing else in the function reads it: holds, spans and their
    arrival kinds, fret-hand placements and their ramps, string count and capo all keep reading the
    presented stream, so **the two forms differ in `notes` and in nothing else** — a contract the
    projection's tests pin member by member. The slide-ramp table moved OUT of the note loop into
    its own pass (`makeSlideRampStarts`) to make that structural rather than careful: the ramps are
    the presented stream's answer, and a hand marker that jumped when the reveal was held would be
    reporting the swap rather than the chart. A view-side end swap was rejected — the presented
    state has already clipped the payload its trims removed, and no lengthening puts that back.
  - **Scored = presented stays structural.** `makeHighwayViewState` composes the projection with no
    form argument, so `ChartNoteForm::Actual` is unreachable from the board, the game and the
    scorer. The reveal is also NON-hit-testable by ruling: hit testing, selection and Alt+click
    insert keep reading the presented projection the controller published (Alt+wheel already acts
    on the selection), and the 3D preview keeps the presented form with its `actual_end_seconds`
    band.
  - **The cost, accepted and flagged.** `EditorViewState::tab_actual` is memoized beside `tab`
    under the same key (arrangement id + chart revision), so a sustain gesture now projects the
    chart THREE times per wheel notch — it was already two, because `makeHighwayViewState`
    composes its own `makeChartViewState` under the same key. Building it lazily would require
    the controller to learn that the reveal is on, which `tab_view.h` forbids ("the controller
    never learns of it"). Fine for a sighting; if Tails is signed, the shape that removes both the
    cost and the form parameter is one producer returning both forms from a single
    `chartResolutions` pass, which would make "equal outside `notes`" a fact of construction
    rather than a test.
  - **Two glyph consequences of drawing a form no rule touched, both accepted, both to sight.** A
    DEAD note grows a tail (rule 4 is a presentation rule and the actual form has none) — Alt shows
    what is STORED, and that tail reads as how long the mute is held. And a shift-slide's arrival
    waypoint, which sits exactly at the presented end and so draws no glyph, sits strictly inside
    the real ring and draws a mid-tail linked continuation head — a mark that appears only under
    the reveal (`linkedWaypoint` is form-relative and correct in both, which its doc now states).
  - **If OUTLINE wins instead**, the outline's cull returns to a prefix maximum over
    `actual_end_seconds`: it currently culls and measures through the actual form's own notes and
    index, which is one authority for a ring's length while both styles ship.
- **C — done 2026-08-22.** Shape spans and their postures are DERIVED from the notes, per chart
  revision, in core: `deriveChartShapes(saved_notes, presented_notes, tempo_map)`
  (`chart/chart_shapes.h`) is the importer's `deriveChordShapes` ported unchanged onto the tempo
  map's own arithmetic, and `ChartResolutions` gained `shapes` and `postures` beside the two note
  forms. The shared arrival rule `chartShapeArrivals` moved into the same header in the review
  pass: with the span rules gone from the validator it was the last thing making `chart_rules`
  know spans exist, and both halves of one derivation now live in one file. Rule 12 and rule 12a
  are exactly as the importer stated them, including the margin trim,
  the floor at the last strum and the exact-adjacency fallback; backlog item #59 (should a span's
  end stop obeying the margin) is untouched and still open. **`templates` left the format with
  `shapes`**, not just the spans: nothing authored a posture, so `ChordTemplate` became
  `ChartPosture{frets}` with `name` and `fingers` deleted, and every consumer branch that could
  only ever read an empty one went with them — `ShapeViewState::name` and the highway's chord-name
  text pass, `ShapeStringViewState::finger` and the highway's fingering panel (with its
  `fingering.png` asset and `HighwayTexture` enumerator), and the timeline ruler's name-chip band
  with `TrackViewport::setShapeLabels`. When names and fingerings are ever authored they arrive as
  a dictionary keyed by a posture, not as fields on one; the arpeggio-bracket posture verb
  (`docs/plans/in-progress/arpeggio-posture-display-options.md`) is still open and two of its
  rejected options now need that dictionary before they could be revisited. The reader refuses a
  document carrying either key, with the re-import remedy named — the same tripwire the removed
  note fields get. The validator's posture and span rules are gone with the authored data
  (`normalizeChordTemplate`, `ChartErrorCode::InvalidTemplate` and `InvalidShape` deleted): derived
  data cannot be invalid.

  **The golden corpus differs in exactly one arrangement, and the old value was the wrong one.**
  Across 113 songs and 245,866 notes, every note, hold, legato verdict and fret-hand placement is
  byte-identical, and every span in 112 songs is too. In one arrangement three consecutive
  all-open dead strums that used to derive three spans now derive one (`3:1`+11/4, `3:4`+3/4,
  `4:1`+5/2 become `3:1`+13/2 — the same end, without the two splits). Root cause: the importer
  derived spans BEFORE `normalizeChart`, and that song's middle strum carried a slide on an open
  string, which the normalizer then dropped ("6 notes: an open string cannot slide, so its slide
  was dropped"). The old spans described notes the chart does not contain. Derived at read time
  the rule sees the settled stream and merges the three, which is what the chart says.

  Two things the review pass changed, neither of them visible in the golden diff. Moving the
  derivation onto the load path put it BEFORE `validateChartRules` — so it now runs against a
  document whose `"string"` values nothing has bounded yet, and sizing the posture array from the
  stream let a corrupt file pick an allocation size (a `"string"` of two billion asks for 17 GB
  and kills the load with the refusal it was about to earn). The width is `g_max_chart_strings`
  now, a constant rather than a quantity read off the input; the tuning would have been no safer,
  since it is equally unvalidated at that point. And `ChartConnections`/`chartConnections` split
  out of `ChartResolutions`: `resolveLegato` reads the saved stream alone, so the settle sweep and
  the `H` verb — which run at every caret move, seek and selection change, and read nothing else —
  were deriving the whole song's presented stream, spans and holds on every keystroke and throwing
  all three away. `ChartResolutions` carries the connections rather than restating them.
- **D — built 2026-08-22, band sighted and superseded 2026-08-23.** The editor preview's
  actual-ring mark: `F1` cycles a floor mark under each marked note running from its onset to
  `ChartViewState::actual_end_seconds`, off → **light** → filled → outlined → off. It is a
  SIGHTING RIG, not a signed look — the numbers below are starting points, and the rig exists to
  decide whether the mark earns a place at all.

  **The first sighting killed the fill and left the outline tolerable**, and the user's own
  reading of why named the replacement: a *mild lighting effect on the floor, like the lighting
  already there, in the colour of the note*. That is the `Light` form, and it is now the
  candidate — the two band forms stay only so the three can be flipped between in place, and the
  loser is deleted with everything only it needed.

  The switch is `HighwayRenderer::setDiagnosticsOptions(HighwayDiagnosticsOptions)`, a draw-time
  POD in its own public header (`highway_diagnostics_options.h`, so the pure rules beside the
  renderer and the editor's preview plumbing can name it without the renderer's whole API) and
  deliberately kept OFF `HighwayDisplayOptions`: those ride the memoized `HighwayViewState`,
  so a toggle the user presses to look at something would re-project the whole chart. The game
  cannot switch it on, and that is a composition-root guarantee rather than a compiled-out one —
  the only caller is `PreviewSurface`, reached from an `EditorCommandId` the game does not link,
  and plan 20 Q5 answer A already ruled against build-define gating for diagnostics.

  **The cull needed a second prefix maximum**, exactly as the 2D reveal did: the note pass's
  visible range keys on `display_hold_ends`, so a note whose presented tail rule 3 emptied leaves
  that range immediately after its onset while its ring is still crossing the board.
  `Impl::actual_ring_prefix_max` is that table, built per chart load in both products.

  **The rules the marks share are stated once, in two pure units beside the renderer.**
  `highway_floor_band.h` holds `highwayVisibleSpan` (how a drawn span is bounded — from the later
  of the onset and the hit line to the earlier of its own end and the horizon, empty when those
  cross, and whether the far end is the span's OWN), `highwayFloorFootprint` (where a mark under
  one note lies on the fret axis and how wide: the note's anchor, or the hand window inset by the
  open-tail margin), `highwayRingMarkApplies` (the per-note gate) and `highwayFloorLightEnvelope`
  (the light's soft-ended alpha, its ramp clamped so the ends never cross). `highway_slide_path.h`
  holds `highwayNoteFretboardX`, `highwaySlideStateAt` and `highwayGlideSliceCount` — the fret
  anchor, the glide, and the density every glide-following mark subdivides by. The sustain tail
  asks all of them too; the marks add no second producer for anything.

  Decisions taken while building, each open to reversal at the sighting:

  - **The light.** Additive `window_light_program` — the board's own lighting sprite, the one the
    hand window, the tapping hand and the strike glow already use — in the note's `StringLaneStyle
    ::tail`, the value the ribbon above it draws with. Additive rather than alpha because a note's
    light sits INSIDE the hand-window light by construction, where an alpha light repaints instead
    of adding. Core half-width 1.5 tail half-widths (0.24 world) with a 2.0 falloff (0.32), so the
    footprint is 0.80 with a 0.16 full-strength middle — inside one fret slot, wider than the tail
    over it. Additive peak 0.22, against the hand window's 0.251 alpha composite. Both ends fade
    over `g_tail_onset_fade_seconds`, the tail's own onset ramp, clamped to half the ring so the
    two ramps meet at the midpoint of a short ring instead of crossing (a fixed ramp left a ring of
    50 ms or less — a dead note's, a chug's — at alpha zero at both of its only two exact times,
    invisible), with NO cap; the far end fades only where the ring genuinely ends on the board. No
    texture: the soft edge is the shader's analytic per-fragment mask, which is
    resolution-independent and already tuned per pass through one uniform.
  - **The light follows the glide; the bands do not.** It walks `highwaySlideStateAt` over the
    visible span, subdividing gliding stretches by `highwayGlideSliceCount` and capped at
    `g_tail_sample_cap` per note. It ignores the tremolo wobble, on the head's precedent (the
    teeth are a texture the tail carries), and past the last waypoint the path holds the last fret
    — which for a note with an unpitched slide-out means the light sits at the trail-off's
    *compressed* end fret while the pick was, in the stored form, still travelling. The board
    carries the ring's LENGTH and no actual-form gesture geometry, so that is accepted rather than
    fixed with a second producer. The bands stay straight: they answer a DURATION question and the
    tail above them already draws the pitch path.
  - **The unpitched release's dim does NOT carry into the light.** A rail dims because pressure is
    coming off the string; the light's whole claim is that the string is still ringing, which is
    the datum the rig exists to read.
  - **Band look.** Plain white `0xFFFFFFFF`, achromatic and in no notation family, reading as
    furniture by PLANE (it lies on the floor) rather than by ink — 3D has no theme seam, and
    exporting `EditorTheme` into `common/ui` so a shared renderer could tint one editor mark would
    invert the dependency. Half-width twice the tail's so a rim shows beside a coinciding tail.
    Fill = 0.16 body plus a 0.5 end cap one attack-line long; Outline = two 0.02-half-width rails
    plus the same cap at 0.35. The cap draws only where the ring genuinely ends inside the window.
    Named constants at the top of `highway_renderer.cpp`; they are what the sighting tunes.
  - **One pass, in the last floor slot.** All three forms draw from one loop after the shape rails
    and both hands' lights and before the note batches, so everything on the floor is under the
    light and lit by it, and the note geometry occludes all three. That also moves the bands past
    the hand-window light, which used to composite over them at its own quarter alpha and left a
    band inside the lit window reading quieter and slightly blue. The bands still fade toward the
    hit line through `color_fade_program`, so a short ring can still dissolve near the hit line;
    the light does not, because `window_light_program` deliberately has no hit-line fade.
  - **Two filters, one gate, positive by default.** `actual_ring_marks_tailed_notes` and
    `actual_ring_marks_chord_members` (`Shift+F1`, `Ctrl+F1`) say WHICH notes are marked, so one
    predicate serves every form. The tailed-note fact is the chart's (`end_seconds >
    start_seconds`), never "a tail is visible this frame", which flickers at the horizon; the
    chord fact is the chord BOX's own rule (`highwayChordBoxApplies`, now named once in
    `highway_view_state.h` and read by the box, the arpeggio lookup, the strike glow and this
    gate), because the reason to filter chord members is that the box already states the hold.
  - **An open note's light tracks the hand, as the tail above it does.** Its footprint is read per
    slice through the one per-time footprint the open tail's stations already walk
    (`floorFootprintAt`, a binary search over the placements), with the window's own ramp samples
    joined to the exact set so the light follows the eased border where the tail does. The BANDS
    still sample once at the mark's start: they draw straight by design. (The first build held the
    light still too, on a recorded reason — "a whole-song placement scan per note" — that was
    false: the window query is logarithmic and the tail was already paying it per sample.)
  - **Toggle, not held.** The 2D reveal is held under `Alt`; this latches. Since the 2026-08-23
    tracking change the held key is a process-wide predicate, so it is not out of reach in the
    preview — the one reason that stands is that a rig watched while navigating with the caret
    keys wants both hands free.

  **No GPU-free witness exists for any mark's appearance** — `HighwayRenderer::draw` has no tests
  at all, by construction. What is tested is the pure math around it: the clamp and its
  `ends_inside` fact, the footprint, the gate across every filter combination, the glide path
  (anchor, waypoint, eased mid-segment, the hold past the last waypoint, a node riding its stop,
  the run-wide unpitched ramp) and the slice policy's bounds. The look is the sighting's job.

  **If a per-note floor light is ever wanted in the GAME, it is a different mark.** This one's
  length is `ChartViewState::actual_end_seconds`, whose invariant is that no game surface reads it
  (ruling 4, scored = presented). A shipped light takes `NoteViewState::end_seconds` or
  `display_hold_ends` instead.

## Rulings recorded (2026-08-21)

1. Held = strict adjacency.
2. No back-compat: every existing chart is re-imported.
3. The 2D hold ribbons change lands as its own commit after A.
4. Scored = presented, a contract recorded here and exposed through the projection; the scorer
   reads it when it exists.
5. Every note's sustain is positive; a dead note carries its notated duration and E25 is a
   presentation rule (the "except dead" wording was serving the same end, and pinning dead at zero
   would have re-broken every legato claim after a muted cluck — the E26 regression reversed on
   2026-08-20). Confirmed 2026-08-21. A quarter-note cap on a dead note's stored sustain was
   considered and declined: nothing visible or audible changes with the length (rule 4 presents no
   tail, playback clucks), the length is exactly the timing information and the legato adjacency
   the stored form exists to carry, and the cap would be one more bound stated in three places for
   a value no surface shows. The same-string clamp is the only bound on any note's sustain.
6. Insert default = one grid step, clamped at the next onset on the string. Rule 12 unchanged.
7. **Proposed 2026-08-21, awaiting the user's confirmation — the lossy-source default.** A
   converted package from the commercial source format stores no duration for a note the charter
   did not mark as held. The plan puts the one unavoidable guess at that import, in the external
   converter: *a source note with no sustain rings to the next onset on ANY string, capped at half
   the kept-sustain bound; a note with a sustain rings for it.* Any string, not its own: rule 1
   presents a ring running strictly past the first binding onset in full, so a chug defaulting to
   its own string's re-strike would draw through every alternating-string riff. Capped strictly
   below the kept bound: a default landing exactly on a quarter would earn a tail under rule 3
   that the source never showed. Consequence to accept with eyes open: the source's sub-quarter
   holds stop drawing, because presentation is one rule for every chart where the package path
   used to skip the Guitar Pro rules. Folds into the stale-package re-export (task #78).
   No accent exception: charters of that format often read an accent as staccato, but the stored
   ring of an accented sub-quarter note changes nothing drawn (the accent glow is the staccato
   read), a shorter default would flatten a hammer-on the charter marked after it, and playback
   can honour the accent itself when it exists rather than the duration storing a convention.
8. **The duration verb is a GESTURE** (ruled 2026-08-22, shipped the same day). A run of steps —
   grid or the Ctrl fine tier, mixed freely — is recorded IN ORDER (amended 2026-08-23, see the
   addendum below; it was ONE accumulated `Fraction` delta as first shipped), and every selected
   note is recomputed by REPLAYING that run over its PRE-GESTURE ring: the answer clamped up at its
   own string's `sustainBoundOf`, and holding the ring it currently has when the replay is not
   positive (there is no empty ring; it rejoins the moment the replay is positive again). The
   user's reason is symmetry: every note replays the same steps from where it started, so whatever
   shape the selection's tails had comes back intact and nothing blocks anything else — a chord
   member pinned at its bound on the way out rejoins its neighbours exactly where it left them,
   where per-step clamping baked the clamp into the next step's starting value and shrank the chord
   asymmetrically. Neither bound enters the replay, which is what preserves that: they judge its
   answer. It also fixes the second half of the same defect: the run is ONE undo entry
   rather than one per keypress. The first step pushes it, every later one replaces it
   (`replaceTop`), so the entry always describes start → now, and the pre-gesture values need no
   snapshot because that entry reversed IS the pre-gesture stream (the settle fold's method). A run
   whose delta nets back to zero has nothing left to describe, so its entry is DROPPED and the chart
   walked back (`dropTop`, the technique toggle's own ending): an entry describing nothing is a dead
   Ctrl+Z on a document reported modified that is byte-identical to the saved file. The
   gesture is live under the technique toggle's own proofs — the selection unchanged, and the burst
   record still owning the history top — and ends wherever the toggle window ends (selection
   change, caret move, any other edit, undo/redo, a committing settle, a save); the next step then
   opens a fresh gesture from the current rings. Both verbs share ONE window field
   (`m_chart_verb_window`, a variant), because at most one can ever be armed.

   **Addendum — a grid step moves the ring's END, so the gesture keeps its STEPS** (user bug
   2026-08-23, fixed the same day). The summed delta was wrong about what a grid step IS: fine-tune
   a tail with Ctrl and every later grid step carried the remainder forever, so the tail never
   returned to the grid the user was looking at. The two tiers move different things and only one
   of them has a size:

   - A **grid** step moves the ring's END — the onset plus the ring, an absolute grid position — to
     the adjacent line strictly beyond it in the step's direction, through the ONE step primitive
     the caret and the lane nudge already walk with (`adjacentTempoGridPosition`). From an on-grid
     end that is exactly one grid step, as the delta was; from an off-grid end it snaps, ceiling
     when growing and flooring when shrinking. No snapping rule is restated in the planner, and the
     step carries the grid NOTE VALUE rather than a beat amount so the meter at whatever measure
     the end lands in scales it (a quarter-note step is one beat in x/4 and two in x/8).
   - A **fine** step adds ±1/960 of a beat to the ring itself, unchanged.

   `ChartSustainGesture` therefore holds the step list, not a delta; `planAdjustSustain` takes it
   and replays it per note. Consequences, all intended and pinned by tests:

   - Grid-only gestures from an ON-GRID ring, and fine-only gestures from any ring, stay exactly
     reversible: each step lands on a lattice its opposite steps back through, and the bounds still
     stay out of the replay.
   - A grid step from a FINE-TUNED ring snaps by design, so reversing it lands on the grid line
     BELOW, not on the off-grid ring the gesture started from. The grid step means "put the end on
     the line"; a remainder surviving it is the bug.
   - A chord whose members sit at different off-grid offsets snaps each member to its OWN next
     line, because the replay runs per note from that note's own end.
   - A run that replays every note back to `base` is still NoChange, so the entry is still retired.
     The undo label names the entry's NET direction — the total change the replay makes to the
     selection's rings, start → now — because the entry describes the whole gesture: grow, grow,
     shrink is a growth of one step and its undo shortens the ring, so a label read off the last
     press ("Shrink") would lie about what Ctrl+Z does. The steps themselves have no sign to sum;
     the entry's own change does.
   - The reversibility claim above was FALSE as first shipped, and the fault was older than the
     gesture: `adjacentTempoGridPosition` stepped one grid step and re-snapped to the NEAREST line,
     which skipped a measure's last line whenever it sat exactly half a step before the next
     downbeat (a 1/4 grid in 7/8: back two beats from the downbeat lands halfway between beats 5
     and 7, and the tie-to-earlier rule picked 5), so a grow-then-shrink took a six-beat ring to
     four. The caret step and the lane nudge had the same defect. The adjacent line is now read off
     the lattice directly (`common::core::adjacentGridPosition`, beside `snapGridPosition` on one
     lattice helper), the editor primitive delegates to it, and the walk is an involution on every
     meter — pinned in both suites by walking the lattice forward and back.
9. **The reveal's outline is FURNITURE INK, not string ink** (ruled 2026-08-22 while building
   stage B, which left the choice open). It draws in `EditorTheme::lane_overlay` at half alpha —
   the translucent white the armed caret square and the Alt insert ghost already share — rather
   than in a dimmed version of the note's own string colour. Three reasons, in order of weight.
   The lane's quieting authority (`Ink` leaned toward the lane ground by `ghosted`) is the
   NOTATION's and is private to the paint core; dimming a string colour in the editor would be a
   second statement of how this surface quiets ink, which is the exact defect class the model is
   clearing out — and exporting the notation palette so chrome could borrow it would breach the
   rule that editor furniture never enters the paint core. The accent is spoken for: it means
   "selected", and a mark in it on every visible note would read as a lane-wide selection. And the
   string identity the ink would carry is already carried by the lane the outline sits in, so it
   would buy nothing. The reveal therefore joins the Alt family it belongs to — what the next edit
   acts on — told apart from its siblings by shape, as they are from each other: a square for the
   caret, a ring for the note-to-be, a tail-height rectangle for the ring being authored. It draws
   at a fraction of their alpha because it marks every visible note at once where they mark one
   slot; the stroke is the marquee's hairline for the same reason.
