# Chart Span and Selection Model — Design Settlement Record

Living record of the chord/arpeggio span semantics, selection granularity, and chart-editing
verb decisions. Each section is marked **SETTLED** or **OPEN**. Replaced decisions are recorded
with their replacement so no stale ruling survives anywhere else in the docs; on final settlement
the results propagate to plan 40 (chart editing), plan 42 (validation hooks), plan 52 (time-range
selection), and `editing-interaction-model.md`, and this document dissolves into them.

> **The placement quantum, not a `Ctrl` fine tier.** Off-grid placement is a session MODE (`Ctrl+G`)
> behind one placement quantum — the grid note value while snap is on, the 1/3840-whole-note tick
> while it is off — read by every position-quantizing verb with no per-verb opt-out. There is no
> `Ctrl` 1/960 tier, so where a section below reaches for one (notably §11's "Ctrl's one meaning is
> precision" clause and §9b's `Ctrl+Alt+arrows` fine step) the live rule is the mode, and the design
> is `docs/plans/completed/grid-snap.md`.

## 1. Onset groups and chord boxes — SETTLED

- Any 2+ notes sharing an onset render as a chord box, in both the 2D tab and the 3D highway,
  regardless of the containing span's classification (chord boxes appear inside arpeggio spans
  too).
- Simultaneity means shared onset. A note ringing into a span (or onto another note's onset)
  from an earlier strike does not participate in the group — e.g. a slid-into note ringing
  while a second note is struck.
- Double stops are conceptually chords: boxed, but unlabeled. Name labels appear only for
  named full-shape groups; labeling every double stop would clutter the views and not every
  double stop has a clear name.
- Span-less stacks are not creatable by design: stacking notes implies a span (section 4).

## 2. Chord vs. arpeggio classification — SETTLED

- **One law, derived from what sounds:** a span is an arpeggio span iff its members sound
  SEPARATELY, and stays a chord-box span only while every sounding of it is the shape whole. The
  span and its posture are derived from the notes, so nothing authors either and nothing can
  disagree with them; the classification is `chartShapeArrivals` (`chart/chart_shapes.h`), which
  asks that one question at the four places a sounding can be incomplete: a posture string carried
  into the span's start still ringing with no onset at it, a CLAIMED member — the stop a right-hand
  onset claims (`claimedStop`) — that no stroke sounds as a voice of its own, a slot inside the span that
  sounds only part of the shape, and a picking-hand
  onset (a tap or a pick slide) sounding anywhere within the span.
- Because the classification reads the sounds, a claim flips the span
  by construction: a member the hand holds that no stroke of its own sounds is the second trigger.
  Display stays fully derived.
- **Importer obligation:** nothing in the format carries a hand shape, so there is no posture for a
  converter to trim to the struck strings; the arrival rule reads what the notes sound. Imports
  author zero claims, so a source's own arpeggio marking survives as the notes themselves and
  nothing else.
- Degenerate spans (single note total, or repeated identical single notes) are neither chord
  nor arpeggio: dropped from display and flagged by validation. Data is not auto-deleted for
  now; automatic removal may graduate later if the ruleset fully hardens.

## 3. Naming — SETTLED (deferred hooks)

- For now 3+ note chords do not require names. Once validation lands (plan 42), consider
  forcing the user to name full chords, with a name-suggestion algorithm (check for an
  existing chord-naming library before hand-rolling).

## 4. Auto-span lifecycle — SETTLED

- Creating a stack (placing a note onto an existing onset) implies the posture and the span in the
  same undo entry as the note insertion.
- Default span extent floor: 1 beat, for tail-less strikes — clamped at creation to §10's
  minimum-sustain-distance limit (`min(1 beat, next outside onset − margin)`) so auto-creation
  never mints an extent the duration verb could not have authored.
- Shrinking a span below a following strike splits the span: the remainder becomes a new span
  with the same posture (rendering as a fresh full chord box, not a repeat). Growing a span
  into an adjacent same-posture span re-merges them. Split and merge are symmetric.

## 5. Span duration, sustains, and the wheel — SETTLED

Current rulings:

- Arpeggio members are **assumed held** for the whole span, exactly like chord members: no
  drawn tail required; the span communicates the hold. Technique-less members in either kind
  of span need the same opt-in override to force-display a tail.
- Alt+wheel on a chord adjusts **only the span** — no temporary member tails shown during the
  gesture.
- Adjusting a **subset** of a chord's members: during the gesture ALL member tails display at
  the span length and only the resized tails move; on release the tails remain displayed and
  the span resizes to the **shortest** sustained member — once any note releases, the shape is
  technically no longer held.
- The same principle applies to arpeggios: shrinking a single member's hold terminates the
  span at that member's end (the shape is no longer physically held). Acknowledged to
  introduce oddities that need working out — see open questions.
- Goal: maximum shared logic between chord and arpeggio span handling; drill until genuinely
  clean.

The unified model — the "explicitness reduction" (a solid starting point; expect refinement from
practice):

- **Display rule (one line):** a note draws its tail iff it carries an explicit sustain.
  Paint computes nothing else — no technique checks, no span inspection. The data is the
  display state.
- **Explicitness conditions** (all subtlety lives at edit-apply time, not paint time). A
  note's sustain becomes explicit when: (1) a tail-requiring technique is applied; (2) the
  user adjusts its sustain; (3) the manual force-show override is toggled; (4) any
  onset-group sibling becomes explicit — the whole group materializes together, because one
  visible tail among hidden-but-held siblings would misread as "only this note sustains".
  Group scoping (not span scoping) is what keeps arpeggios and chugged chord spans from
  sprouting a forest of tails: an arpeggio member's group is itself alone; a technique on one
  chug strike lights only that strike's group.
- **Implied hold definition:** a member without an explicit sustain is held until its
  string's next onset within the span, else to span end. This is also the value a sibling
  materializes at when its group goes explicit (chug strikes materialize to next-strike, a
  single-strike chord's members to span end).
- **Normalization:** a group whose members are all uniform at their implied hold,
  technique-free, and override-free collapses back to implied (explicit sustains cleared) on
  apply — save==export normalization precedent. Span extent is stored; when a string's
  final sounding is explicit, normalization clamps the extent to the earliest final
  hold end (min rule). Mid-span staccato never terminates a span: only shortening a string's
  last sounding does, so a re-struck shape survives a short strike.
- **Targeting rule for the wheel** (both span kinds): selection covering all sounded notes of
  the span → adjust span extent; a proper subset → adjust those members' tails (materializing
  their group). A chord's single click selects its whole group, so chords get span adjustment
  for free. **Signed affordance:** double-click any member selects the span's full note set,
  giving arpeggios the same easy path (rails-click-as-span-object deferred until a
  delete-span verb needs it).
- **Shrink-split shared primitive (signed, provisional for arpeggios):** early termination —
  from span shrink or a final-sounding release — splits later-onset members into a new same-posture
  span; members struck before the break stay, and their tails may legally ring past the span end.
  For arpeggios this is the most reasonable known behavior but is explicitly expected to be
  revisited after hands-on practice; treat as a standing watch item when implemented.

Display is per-group via explicitness, never span-wide by fiat: the reduction supplies the
mechanism the "all tails display" phrasing above only sketches, and the outcomes are the same.

## 6. Ghost note rework — SETTLED

- The ghost is the full-note representation: colored head, fret number, at the snapped position.
- The ghost carries no sustain; holds are set after placement via the wheel (section 5).
- **§9's caret model replaced this mechanism.** Typing at the caret is how a fret reaches a new
  note, so the composable Alt ghost quasimode — the pending fret published through the edit view
  state, the Alt+digit composition handler, the Alt-release session bound, and the run
  accumulation — does not exist. **Nothing of the ghost survives** (2026-09-11): the lightweight
  hollow ring §9's amendment kept went with the chart's pointer authoring, since a press creates
  nothing there now. What `Alt` shows while it is held is the ring REVEAL — every visible note's
  actual stored ring — and the only entry preview is the typed digit's own pending box.

## 7. Selection granularity — SETTLED

- **Containment hierarchy:** a single click selects the **individual note**, a double click its
  whole onset group (its chord), a click on a span's rails/bracket will select every note in the
  span (rides the span slice), and a double click on the span will open its name/fingering editor
  (future — the "template editor lite" that also serves forced naming and the fuller-shape
  arpeggio case). Each click level selects one containment level up: note ⊂ chord ⊂ span.
  Rationale for preferring this over a chord-unit click: fret correction on one chord member
  proved the most common single-note edit and three designs (focused member, transpose typing,
  Ctrl-isolation) were burned working around a group-selecting click; cohesion lives at the verb
  level (verbs act on whatever is selected), not in the click. Ctrl+click toggles individual
  membership; marquee remains geometrically precise; inserting selects the placed note.
- **Right-click context menu:** the chart menu is a **keybind-discovery menu** — every applicable
  action listed with its **live shortcut** (via the plan-46 registry) — so its value is *teaching
  the keys*, and the "redundant with a direct gesture" objection is the wrong test for it;
  techniques will only *add* entries later. (A no-modifier hover ghost stays rejected as a lying
  affordance; if the menu ever needs a position marker, the white ring appears only while it is
  open.)
- **"Note properties" dialog dropped:** derived-over-authored leaves no per-note metadata needing
  a form; techniques become selection toggles; chord metadata lives in the span dialog; bend
  curves get a direct-manipulation editor when techniques land.
- **Fret typing = set exact; fret movement = Alt+Shift+wheel (FINAL,** with the two designs it
  beat recorded here for the reasoning trail): slice 1 exposed that fret is per-string data. A
  *focused-member* model (single-member digit targeting, blinking underline) was rejected —
  asymmetric (no multi-member retype) and the indicator never looked right. A *transpose-typing*
  model (plain digits move the shape's lowest fret to the typed number, Ctrl for exact) was
  rejected — typed-number-differs-from-displayed-number would confuse newcomers, and no modifier
  assignment survived scrutiny (Ctrl inverted its precision meaning under the flip; Shift+numpad
  is unfixable at the JUCE level — the Windows NumLock legacy delivers navigation VKs and JUCE
  discards the lParam that could disambiguate). Final: **typed digits set every selected note to
  the exact value** (what you type is what appears; multi-digit window unchanged; Ctrl+digit
  unbound) and **Alt+Shift+wheel shifts the selection's frets ±1 per tick**, shape-preserving by
  construction, refusing (never clamping) when the lowest fret would pass zero or the highest the
  cap. The relative operation lives on the naturally relative gesture; no second selection concept,
  no focus indicator.
- Shift+click creates a **time-range selection** (Guitar Pro-style): one big timespan highlight,
  **replace** semantics, anchored at the last non-Shift selection action; Shift+clicks while held
  re-extend from that anchor; with no prior anchor the first Shift+click acts as a plain click. It
  is a mutually-exclusive **kind** of the one selection (making it clears any object selection) and
  is **strictly grid-locked** on pointer *and* keyboard: a boundary is never off-grid
  (`Ctrl`+ruler-drag = measure-snap, not off-grid; amends plan 47). Transfers to plan 52.

## 8. Arpeggio conversion — SETTLED

- The fretting hand's stop under a right-hand onset (`held`) states a shape member the picking hand
  never sounds: the string and fret join the hand's posture without a played note there. The span
  classifies as an arpeggio automatically, and the existing posture rendering (unsounded members)
  displays it. This resolves the "display a fuller shape than the notes play" case without a
  dedicated template editor, for every string a right-hand onset reaches. Stating a stop on a string
  where NOTHING sounds waits on plan 60's span templates (RULED 2026-09-17).

> **How the member is stored, and what the span is.** A claimed member is the `held` field on an
> onset the picking hand stops the string for — a plain tap or a pick slide — or the pressed `fret`
> of a tapped harmonic, read through one query (`claimedStop`) and resolved against the DERIVED span
> at read time.
> There is no authored template in the format, no extent that belongs to one, and no
> template-relative comparison: the span and its posture are derived from the notes
> (`deriveChartShapes`), and the arpeggio flip is carried on the span the derivation resolved the
> member into. A separate slot-keyed array for these members was rejected — a second array has to
> be kept disjoint from the notes by a rule that slot uniqueness already states. The full record of
> why, including the options that lost, is `docs/plans/todo/arpeggio-authoring.md`; the maintained
> spec is rule 12b in `docs/developer/the-project-lifecycle.md`.

## 9. The caret model — SETTLED (the Guitar Pro editing posture)

Signed after hands-on Guitar Pro comparison. One position concept per transport state: while
**paused**, the caret (grid position × string) is THE position — where editing happens, where
typing inserts, where play starts; while **playing**, the playhead is THE position (the moving
indicator). They are never visible together, which resolves the objection that killed the
first caret. The paused playhead is gone.

> **Amended by §9a (the two-state marker):** the caret survives as the *armed* state of a
> position marker whose *passive* state is the returned paused cursor. Where §9a's rules differ
> from the bullets below, §9a wins; §9a's closing list names each superseded clause.

- **Caret existence:** always present once a chart is displayed (song start, string 1, on
  load). Esc clears the note selection; the caret persists. On an empty grid slot it renders
  as a white square outline — editor furniture distinct from every circular note shape, and
  visible over accent glows; on a note, the note's selection highlight IS the caret display.
- **Placement:** clicking the highway band (the waveform/tab lane) places the caret at the
  position the placement quantum gives on the clicked string lane, deselecting notes; clicking a
  note selects it AND co-locates the caret (selection and caret are always co-located). Clicks in
  the tone strip or automation lanes do NOT move the caret or seek — the highway is the caret's
  only pointer surface (plus the ruler, which keeps positioning the timeline). Ruler clicks
  position the caret at the snapped time on the remembered string.
- **Typing inserts:** digits on an empty caret insert a note there with the typed fret; the
  multi-digit window continues onto the just-inserted note (2 then 3 → one insert of fret 23,
  ONE undo entry — the widened entry stays an insert, never degrading to a retype that would
  strand the note under undo). Digits with a note selection retype it (unchanged). The
  "plain keys never mutate" rule is not the grammar's foundation — the caret model supplies the
  deliberateness (typing visibly lands at the caret); the drag-threshold and Alt-gate rationales
  stand on their own for the verbs that keep them.
- **Arrows are caret movement:** plain Left/Right step the caret along the grid on its
  string; plain Up/Down move it across strings; **Ctrl+Left/Right jump to the previous/next
  measure** (Guitar Pro's jump). Arrow movement re-derives the selection from what sits under the
  caret: a note → selected; empty → selection empty, white square. Alt+arrows still MOVE the
  selection; Alt+Shift keeps the axis rule (horizontal extent, vertical fret).
- **Shift+arrows = caret-anchored time selection:** `Shift+Left/Right` extends a grid-locked span
  by the display grid, `Shift+Ctrl+Left/Right` by measure, `Shift+PageUp/Dn` by section,
  `Shift+Home/End` to chart bounds; `Shift+Up/Down` is unbound. The anchor snaps to grid even from
  an off-grid caret. It is a mutually-exclusive **kind** of the one selection — making it clears
  any object selection. Releasing Shift keeps the range; a plain arrow clears it; Shift again
  resumes. Builds when plan 52's range object lands.
- **Transport:** Space plays FROM THE CARET. On pause, the playhead hides and the caret
  snaps to the nearest grid line to the stop position, on the remembered string. The
  playhead renders only while playing.
- **Deleted by this model:** the entire Alt insert quasimode — the colored composable ghost,
  Alt+digit fret composition, the insert-session accumulation, the edit view state's pending
  insert fret, the CopyingCursor, the ghost repaint strips, and sticky last-fret (inserts carry
  exactly the typed digits, nothing else). Alt returns to being purely the mutation gate for
  move/duration/fret-shift. (The full-note ghost of §6 shipped first and was superseded
  knowingly — the caret workflow is less machinery and faster entry.)
  > **Amended** (Alt+click chart-note create): the *heavy* quasimode above stays deleted, but
  > Alt+click on an empty slot is the lightweight mouse form of §9b's Insert verb — it plants a
  > **fret-0 note** (corrected by the caret's normal typing rule; no Alt+digit composition, no
  > accumulation, no sticky fret) and shows a hollow white ring ghost while Alt is held. So "Alt
  > returns to being purely the mutation gate" holds on objects but not on empty slots, where Alt
  > is the neutral-create gate — uniform with the tone surfaces. Full record in
  > `editing-interaction-model.md`.
  >
  > **Amended again 2026-09-11 (every note is TYPED):** the amendment above is retired outright —
  > `Alt`+click plants nothing on the tab lane, and neither does any other press. A chart press,
  > under every modifier, arms the caret and selects what is there. So "Alt returns to being purely
  > the mutation gate" holds again on this surface, with one exception that is a KEY rather than a
  > click: `Alt`+digit at a ring's exact end creates the slide-out. The white ring ghost is gone
  > with the gesture it previewed.
- **Unaffected:** the containment click hierarchy on notes, Ctrl toggle, marquee, Shift+click
  time range (now caret-anchored, matching GP), all Alt / Alt+Shift verbs, delete, undo,
  zoom, the selection-verbs-follow-the-selection rule.
- **Built (stages A and B):** caret core, typing-inserts with the widened-insert undo rule, arrow
  movement with the measure jump, play-from-caret, pause/stop snap, playback-only playhead
  (chartless arrangements keep their paused playhead — no caret exists to replace it),
  highway-band-only seek clicks, paused seeks carrying the caret, and caret-address resume
  persistence (exact measure:beat:offset + string, per the standing ruling that no time
  recalculation is ever involved; chartless projects persist the nearest grid line to the
  transport).
- **Playback dissolves the caret:** play clears the note selection and the caret stops publishing
  while the transport plays; pause snaps it back on the remembered string. One position concept
  per transport state, with no residue of the other.

### 9a. SETTLED — multi-select stays; the two-state marker

The full-GP question is answered: **Guitar Pro's arity-one selection is declined; its caret
survives as one state of a two-state position marker.** The deep analysis (research over GP
8's manuals, MuseScore 4, TuxGuitar, DAW piano rolls, and multi-caret text editors, plus an
adversarial workflow battery) found that GP's *feel* lives in the caret — keyboard-first
navigation, immediate typing, no modes, all shipped by §9 — while GP's *bulk-editing annoyance*
lives precisely in the selection limits pure-GP would import: arity-one typing, contiguous
full-stack ranges, no sparse sets (GP itself is caret plus beat-range selection plus dialog
string-mask wizards; TuxGuitar's caret-only decade produced its top-voted feature request asking
for exactly our multi-select). The user's motivating workflow — marquee the middle notes of every
chord in a repeated progression and retype them in one gesture — is unrepresentable under pure GP.
A multi-caret variant (typing applies at every caret, text-editor style) was examined and
rejected: it breaks §9's one-position invariant, and every consistent caret-spawn rule collapses
it back into the selection model.

**The two-state marker.** One position marker exists at all times. It is either **passive —
the cursor**, the plain paused-playhead line resting at the transport position (only the
string that arming lands on is remembered), or **armed — the caret**, §9's editing caret
owning an exact grid slot × string. Handoffs:

| Event | Marker afterwards |
| --- | --- |
| Plain click on an empty slot or a note (paused) | Armed there; a note click also selects it |
| Arrow key while passive (paused) | Armed at the cursor on the remembered row (the string, or the lane while it is still visible) — at the exact slot the editor last put the cursor at while the transport still stands there, else the nearest grid line; the first press arms in place, later presses move (Up/Down walk the focus rows instead, 2026-09-13) |
| Any multi-select gesture — Ctrl+click, double-click, marquee, and every future gesture whose result is a multi-note selection (span-rail click, §5's member double-click, plan 52's range once it selects notes) | Passive; the cursor takes the caret's place (a paused seek to the caret's musical time), signalling that verbs now act on the highlighted selection, not a caret. Dissolution is a *rule over outcomes*, not a closed gesture list — so a marquee dissolves when its box **resolves with notes** at release, and an empty box is a complete no-op: no selection outcome, so an armed caret (and the standing selection) survive untouched |
| Esc | Armed → passive in place, selection kept; passive with a selection → the selection clears; either rung also ends the multi-digit fret-entry window |
| Play | Passive — playback dissolves the caret and clears the selection; the cursor is the moving playhead. Space starts playback from the marker in both states |
| Pause / Stop / paused seek | Passive at the transport position; the cursor rests at the raw stop point — no grid snapping while passive, snapping happens at arming |

**Invariants** (structural — enforced at the mutation points, never re-checked downstream):

- Armed ⟹ paused. The transport listener demotes the marker on any playback start, so even
  an externally started transport cannot leave an armed caret behind.
- Armed ⟹ the selection is exactly what sits under the caret (empty on an empty slot, that
  one note on a note). Every multi-note selection implies passive.
- Passive ⟹ the marker's time IS the transport position. Dissolution seeks make this hold,
  so the paused cursor line renders from the transport with no separate position plumbing,
  and Space-resume after any handoff is continuous.
- The caret square (white, slightly rounded corners) renders iff armed — on an empty slot it
  marks where typing inserts, and on a note it rides the selection highlight so the caret
  stays visible through a single selection. **Square on an empty slot ⟺ typing inserts; square
  gone ⟺ verbs act on the highlighted selection.**
- The cursor draws in two layers by transport state (the behind-content rule): while **paused**,
  a play-from-here column at the marker's position draws **behind every track-row component**
  (over the grid, under the notes — visible in every gap, never covering a fret number; muted
  `paused_cursor` theme color, 1px, same rounding as the shared cursor draw so it lands in the
  ruler mark's exact pixel); while **playing**, the overlay's moving line draws in front. While a
  caret is armed the column rides the caret's slot, with the square's vertical span cut out of the
  column itself — ONLY the cursor hides behind the caret, so the grid dots and string lines the
  square overlaps keep showing through it. The **ruler's aligned flag mark is ALWAYS shown**: the
  moving playhead while playing, else the marker — the armed caret's slot (Space seeks there
  first) or the passive transport rest; the body line (tip-aligned flag centered on its exact
  pixel) takes the paused color while paused so line and column read as one continuous indicator,
  while the flag triangle stays playback white in both states so the play-from-here mark never
  loses visibility. The lane's only in-front paused furniture is the caret square. Chartless
  arrangements keep their in-front paused line as their only indicator. The dissolution seeks
  stay: they keep Space, the ruler flag, and the behind-column at the former caret's spot.
- Wheel zoom centers on the marker: the armed caret when one exists, else the transport
  cursor (the playing playhead or the passive paused cursor) — the position concept and the
  zoom anchor are always the same thing.
- **Undo/redo never move the marker:** reveal-on-undo scrolls the viewport to the restored
  content; it never arms, disarms, or seeks. This keeps the passive invariant from forcing a
  transport seek on every Ctrl+Z, and an armed caret simply re-renders against whatever undo put
  under it.
- **Caret navigation keeps its measure in view:** whenever the caret lands at a new time in a
  measure that is not fully on screen, the window glides — the same eased shift playback follow
  uses — by the minimal amount that fits the whole measure (left-aligned when it starts before the
  view, right-aligned when it ends past it), overshooting the aligned edge by a **4%-of-view
  reveal** so a note seated exactly on the neighboring measure's boundary — legal here, unlike
  Guitar Pro — shows its whole head plus a sliver of that measure's interior (a view fraction, not
  pixels or musical time: heads are fixed-pixel so a musical buffer dies at low zoom, and a
  fraction keeps the perceived peek constant across window sizes; 4% clears a full head width at
  any realistic view). When the measure plus reveal cannot both fit, the full measure wins and the
  reveal compresses. A measure wider than the view keeps the caret itself in view with a small pad
  instead. String-only caret moves glide nothing, and the rule fires on caret movement only — a
  user scroll away from the caret is never yanked back.
- **Paste arms the marker** (working answer for plan 40 Phase 9 / plan 52, to be ratified at
  G52-RANGE-EDIT): pasting while passive first arms at the nearest grid line — snapping
  happens at arming, exactly as for arrows — then rebases the clip there; the pasted notes
  become the selection, which (when multi-note) dissolves the marker per the standard rule.
  Paste never authors off-grid content from a raw passive rest.

**The typing rule** (one rule, no modes): digits retype the selection when one exists — every
selected note takes the exact typed value, multi-digit widening unchanged — else they insert
at the armed caret; while passive with no selection they are **inert**, so a stray digit
after listening can no longer author a note. Arrows while playing are equally inert, and lane
clicks while playing are plain seeks (the lane behaves like the waveform until pause).

**The uniform-scope law** (standing principle, binds every future verb): *no verb ever
declares its own scope — scope is the selection.* Chord scope comes from the containment
hierarchy (§7), sparse sets from marquee/Ctrl, contiguous bulk from plan 52's time range.
Corollary: on a selection spanning several spans, §5's wheel-targeting rule applies per span
independently (full sounded set → span extent; proper subset → member tails).

**Mixed-validity policy for technique toggles** (Phase 5; the no-invalid-states principle):
validate per note before applying. Independent per-note toggles (hammer-on, palm mute, …)
apply where valid with explicit feedback ("applied to 7 of 8 — the first note has no
preceding note"); shape-coupled transforms (fret shift) refuse the whole set, as shipped.
Never silent partial application, never blind toggling of mixed states, and no invalid chart
state — GP's invalid "h" hammer-on is the named counterexample — is ever authorable.

**Selection-count chip:** with two or more notes selected, a readout ("5 notes") joins the
transport readout row. The caret's absence says "bulk mode"; the chip says how big, so an
off-screen selection cannot be forgotten under the typing rule.

**Resume persistence** (supersedes §9's caret-only storage): one tagged marker per project in
app-local settings — armed as the exact musical address (`caret:measure:beat:num/den:string`,
no time math, per the standing ruling) or passive as the raw transport time
(`cursor:seconds:string`, seconds being the passive marker's native coordinate). Restore
re-creates the stored state exactly. A stored caret whose string no longer exists on the
loaded chart — or that belongs to a now-chartless project — demotes to a passive cursor at
the same musical time: restore never clamps onto a wrong string and never invents a position.

**Supersedes in §9:** "caret always present once a chart is displayed" (the *marker* is
always present; the caret is its armed state); "pause snaps the caret to the nearest grid
line" (pause rests the passive cursor at the raw stop point); "ruler clicks position the
caret" and paused seeks carrying the caret (transport motion demotes to passive); "Esc clears
the note selection; the caret persists" (the Esc ladder above); the paused playhead's
unconditional absence (it returns while passive). Everything else stands: typing-inserts and
the widened-insert undo rule, arrow stepping with the measure jump, play-from-the-marker, the
highway-band seek gate, and chartless behavior (now simply "the marker never arms").

## 9b. The marker's row axis + unified selection — SETTLED

The full grammar record (verb table rows, per-surface behavior, amendment record) lives in
`editing-interaction-model.md` — this section holds only what extends the marker model itself.

- **Rows, not strings.** The keyboard's vertical coordinate generalizes from a string index to a
  **row** (re-ruled 2026-09-13, `docs/plans/completed/keyboard-focus-rows.md`): the ruler's
  section, tempo and time-signature rows, the chart strings, then the **tone-region row** (see
  *Tone-region row* in `editing-interaction-model.md`), then the visible automation lanes (a lane
  row is identified by instance + parameter, never display index), then the "+" row. The armed
  caret rides only the strings and the lanes; the marker rows and the "+" row are reached by
  selection, the caret demoted in place. Plain Up/Down
  step one row; `Ctrl+Up/Down` jump to the nearest row of the adjacent group; `Shift+Up/Down` is
  unbound (the time range is full-height); Left/Right and Ctrl+Left/Right behave identically on
  every point row, and from a selected row they arm in place on the remembered row, the lane
  included. Clicking an automation lane seeks and arms
  the caret at the nearest grid line on that lane. A lane leaving the visible set dissolves an
  armed caret on it to passive (the §9a demotion posture: never clamp onto a wrong row, never
  invent a position).
- **The caret steps the union stop set** (the off-grid unification, full record in
  `editing-interaction-model.md`): plain Left/Right stop at the nearer of the adjacent grid line
  and the row's next authored object (a note on the string, a point on the lane), so off-grid
  objects are first-class, keyboard-reachable stops — landing arms onto them. Between stops the
  caret still never rests; fine *positioning* is authoring, not navigation, and it is reached by
  turning snap off so the placement quantum becomes the tick, the caret riding the object it
  nudges. This replaces the "off-grid notes are selectable but never caret slots" posture.
- **The typing rule generalizes by payload** (one rule, no modes): digits at an armed empty
  slot author the row's payload — a typed fret on a string row, the typed-value editor (seeded
  with the digit) on a lane row. Alt+arrows at an armed empty lane slot create-then-nudge, with
  the created point landing **on the curve** (sonically silent until pulled) — the keyboard
  mirror of the on-curve Alt+click placement. **Insert is the neutral-create verb everywhere**:
  fret-0 note / on-curve point at an armed empty slot, and a tone-change split on the tone-region
  row (the "empty slot" is a region interior — a create, not a mutation); no-op on occupied slots
  and while passive, so Insert never mutates an existing object — **with one named exception: a
  filled *plugin slot*, where Insert = replace-with-confirm**. Its **mouse form is Alt+click** on
  an empty slot: the same fret-0 note / on-curve point, previewed by the Alt-held insert ghost
  (the tab lane's white ring, the lane's on-curve ring), so the neutral-create gesture is uniform
  across every surface's pointer *and* keyboard.
  > **Amended 2026-09-11 (the chart's entry grammar — every note is TYPED):** the chart leaves the
  > neutral-create rule entirely. `Insert` authors nothing there and no press creates, because a
  > chart object always carries a fret and there is no neutral value to plant without one; the
  > Alt-held white ring ghost is gone with the gesture it previewed. The
  > DIGITS are the whole of chart entry: a head on an empty slot and at a ring's exact end, a point
  > on the path where a ring covers the slot, and — under `Alt`, in that one cell alone — the
  > slide-out at a ring's exact end. Dividing a ring is two keystrokes, the digit that plants the
  > point and the `Shift+L` that disconnects it, so nothing single-press truncates a ring or clips a
  > keyframe. "Insert never mutates an existing object" therefore governs the lanes, the tone row
  > and the chain, with the plugin slot as its one exception. Full record in
  > `editing-interaction-model.md`.
- **One selection editor-wide — two kinds.** The single editor-wide selection is, at any moment,
  one of two mutually-exclusive kinds: an **object selection** (chart selection, automation
  point-set, or tone region — alternatives of one editor-core sum type) *or* a **time selection**
  (a grid-locked full-height span). Making one clears the other; two live selections are
  unrepresentable; there is no Delete precedence ladder (Delete deletes *the* selection,
  dispatching on kind); the §9a selection-count chip generalizes to the kind. The loop region is a
  separate transport state; the **plugin chain is a separate modal focus scope**, NOT a member of
  this selection (drilling in parks it, does not clear it). Deliberate clicks arm the caret onto
  the clicked object on both surfaces — automation point clicks alongside chart note clicks;
  selection replacement that arrives as a side effect of another gesture moves the marker nowhere.
- **Supersedes**: the "tone/automation surfaces never move the caret" ruling (it predates the
  caret having rows there) and §9a's implicit string-only row space.

## 10. Minimum sustain distance — SETTLED (restriction); overrides OPEN

Extending a note's tail (the duration verb — and span extents when slice 3 builds them) clamps
to end at least a **margin before the next onset on ANY string**, not just the same string:
tails must never crowd another note. The margin is 1/16 of a whole note (a quarter beat in
x/4 — the shared constant in `grid_arithmetic.h`; 1/32 was trialed and reverted on sight) —
intended to become configurable, but the setting ships with the override design below, not
before. Rules:

- Same-onset chord members sit at equal positions and never block each other, and **span
  siblings never block each other**: notes under a shared shape span are implied-held across
  each other's onsets (§5), so a member tail extends freely past sibling onsets — the first
  later onset outside every shared span is the one that binds. The scoping is what keeps an
  unscoped any-string clamp from making §5's member-tail adjustment refuse at one grid step
  inside every arpeggio.
- The clamp is **duration-verb ergonomics, never chart validity**: imported charts keep
  whatever spacing they have, the insert truncation (40-Q2-B) still truncates an existing
  tail to end exactly at the inserted note (shortening to make room is not crowding), and
  moves/merges are unclamped. Promoting the margin to a validity or lint rule would route
  through plan 42's corpus calibration first. A tail already at or past the limit refuses to
  grow; it is never shrunk by a grow gesture. (The GP importer trims its own generated tails to
  this same margin at build time — a builder normalization policy for GP's full-notated
  durations, not a validity rule; .rock imports arrive untouched.)
- **OPEN — override design (deliberately deferred):** the clean ways to intentionally exceed
  the limit (and the exact cases that justify it), plus the margin's configurability surface,
  are a separate design discussion. Until it happens the blanket restriction stands as the
  safe default.

## 11. Grid-default chart authoring — SETTLED

**The grid is the default lattice, not a capability wall.** Every chart-note authoring verb
snaps to the displayed grid's own exact rationals by default: placement snaps to it, moves
step it whole, sustains grow and shrink by it; finer or tuplet *lattices* come from choosing a
finer or tuplet grid note value (the free-text grid box accepts any fraction with terms in
[1, 128]; 1/6, 1/12, and 1/24 are presets so triplets are one click away). Off-grid authoring is
reached by the session's snap mode rather than by a modifier: with snap off the placement quantum
is the tick, so note moves, automation points, tone-region boundaries and ruler seeks all reach
imported performance timing (sub-1ms, arbitrary offsets) and can correct it without quantizing.
The default stays hard-snapped because rhythm is the chart's judge — snap *default* follows the
data's judge; the *capability* is uniform, and it is uniform because one quantum answers for every
surface.

- **Relative moves:** grid-step moves of off-grid notes keep their offsets — authoring
  restrictions never silently rewrite existing content (the same posture as §10's margin). The
  format keeps arbitrary rationals; imports lose nothing.
- **Ctrl:** on chart clicks Ctrl means the selection toggle, and on plain arrows the measure
  jump. It carries no fine-grid tier of its own — precision is the snap mode's.
- **Preset naming (deferred):** the grid dropdown shows raw fractions ("1/12"); provide
  friendlier REAPER-style names ("1/8 triplet") in a future pass — recorded in the
  interaction-model doc's deferred decisions.
- **Supersedes:** §9's original placement clauses; the caret-step re-snap exists for grid
  note-value switches and for stepping off an off-grid stop (union stop set, §9b).

## Build order (once section 5 settles)

1. Selection granularity — chord-unit click + Ctrl precision (editor-core). *Built.*
2. Classification v2 + universal chord boxes — both projections; importer-normalization
   companion task in the converter tool.
3. Auto-span lifecycle + the duration verb — span creation in planners, split/merge,
   wheel-tick coalescing (same replaceTop pattern as fret typing), tail-visibility rules.
4. Ghost rework — *superseded by §9: typing at the caret replaced the ghost mechanism entirely.*
5. Shift+click time range — recorded in plan 52; built when 52's operation semantics get
   their sign-offs.

The Phase 4 remainder (pointer drag-move of selected notes + Esc drag-cancel) is a deliberate
long-term item, scheduled in `docs/plans/todo/tab-pointer-drag-editing.md` rather than in this
build order — the off-grid unification dissolved the rationale for parking it. Slice 2 is the next
execution step.
