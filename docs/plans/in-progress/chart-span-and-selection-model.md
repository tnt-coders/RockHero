# Chart Span and Selection Model — Design Settlement Record

Living record of the chord/arpeggio span semantics, selection granularity, and chart-editing
verb decisions settled in discussion on 2026-07-17. Each section is marked **SETTLED** or
**OPEN**. Reversed decisions are recorded with their replacement so no stale ruling survives
anywhere else in the docs; on final settlement the results propagate to plan 40 (chart
editing), plan 42 (validation hooks), plan 52 (time-range selection), and
`editing-interaction-model.md`, and this document dissolves into them.

## 1. Onset groups and chord boxes — SETTLED

- Any 2+ notes sharing an onset render as a chord box, in both the 2D tab and the 3D highway,
  regardless of the containing span's classification (chord boxes appear inside arpeggio spans
  too).
- Simultaneity means shared onset. A note ringing into a span (or onto another note's onset)
  from an earlier strike does not participate in the group — e.g. a slid-into note ringing
  while a second note is struck.
- Double stops are conceptually chords: boxed, but unlabeled. Name labels appear only for
  named full-template groups; labeling every double stop would clutter the views and not every
  double stop has a clear name.
- Span-less stacks are not creatable by design: stacking notes auto-creates a span (section 4).

## 2. Chord vs. arpeggio classification — SETTLED

- **Template-relative rule:** a span is a chord span iff every onset group inside it sounds
  the full template. Otherwise it is an arpeggio span.
- Auto-created templates match their stack exactly, so editor-authored chords classify as
  chords without any extra input; deliberately extending a template past the played notes (the
  arpeggio conversion, section 8) flips the span to arpeggio automatically. The template is
  the intent carrier; display stays fully derived.
- **Importer obligation:** imported charts may carry templates listing more strings than a
  passage strikes without arpeggio intent. The importer normalizes — trim templates to the
  struck strings unless the source marks the hand shape as an arpeggio (verify the exact
  source field in the converter tool when implementing). This work lands in the external
  converter tool as a companion task.
- Degenerate spans (single note total, or repeated identical single notes) are neither chord
  nor arpeggio: dropped from display and flagged by validation. Data is not auto-deleted for
  now; automatic removal may graduate later if the ruleset fully hardens.

## 3. Naming — SETTLED (deferred hooks)

- For now 3+ note chords do not require names. Once validation lands (plan 42), consider
  forcing the user to name full chords, with a name-suggestion algorithm (check for an
  existing chord-naming library before hand-rolling).

## 4. Auto-span lifecycle — SETTLED

- Creating a stack (placing a note onto an existing onset) auto-creates the template and span
  in the same undo entry as the note insertion.
- Default span extent floor: 1 beat, for tail-less strikes.
- Shrinking a span below a following strike splits the span: the remainder becomes a new span
  with the same template (rendering as a fresh full chord box, not a repeat). Growing a span
  into an adjacent same-template span re-merges them. Split and merge are symmetric.

## 5. Span duration, sustains, and the wheel — SETTLED (2026-07-17)

Current rulings (2026-07-17, latest revision — several reverse earlier ones):

- Arpeggio members are **assumed held** for the whole span, exactly like chord members: no
  drawn tail required; the span communicates the hold. Technique-less members in either kind
  of span need the same opt-in override to force-display a tail.
- Alt+wheel on a chord adjusts **only the span** — no temporary member tails shown during the
  gesture. (Reverses the earlier tails-and-span-in-lockstep ruling.)
- Adjusting a **subset** of a chord's members: during the gesture ALL member tails display at
  the span length and only the resized tails move; on release the tails remain displayed and
  the span resizes to the **shortest** sustained member (reverses the earlier longest-member
  ruling — once any note releases, the shape is technically no longer held).
- The same principle applies to arpeggios: shrinking a single member's hold terminates the
  span at that member's end (the shape is no longer physically held). Acknowledged to
  introduce oddities that need working out — see open questions.
- Goal: maximum shared logic between chord and arpeggio span handling; drill until genuinely
  clean.

The unified model — the "explicitness reduction" (signed 2026-07-17 as a solid starting
point; expect refinement from practice):

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
- **Normalization (signed):** a group whose members are all uniform at their implied hold,
  technique-free, and override-free collapses back to implied (explicit sustains cleared) on
  apply — save==publish normalization precedent. Span extent is stored; when a string's
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
  from span shrink or a final-sounding release — splits later-onset members into a new
  same-template span; members struck before the break stay, and their tails may legally ring
  past the span end. For arpeggios this is the most reasonable known behavior but is
  explicitly expected to be revisited after hands-on practice; treat as a standing watch
  item when implemented.

Current rulings above that the reduction supersedes in mechanism (not in outcome): the
"all tails display" phrasing — display is per-group via explicitness, never span-wide by
fiat.

## 6. Ghost note rework — SETTLED (built 2026-07-17)

- The ghost returns to the full-note representation: colored head, fret number, at the
  snapped position (supersedes the white-ring ghost).
- While Alt is held, digit keys edit the ghost's fret using the same 750 ms multi-digit
  widening grammar as real fret entry; ghost edits never touch the undo stack. Commit on
  click is one undo entry.
- The composed fret persists across placements within one Alt hold. All notes placed during a
  single Alt press accumulate in the selection; placing into an existing stack selects the
  whole resulting group.
- The ghost carries no sustain; holds are set after placement via the wheel (section 5).
- Ghost state moves from the view into the controller (published through view state) since
  keys now edit it.
- As built: the pending fret is `m_chart_last_fret` published through
  `ChartEditViewState::insert_fret`; Alt+digits compose it via `onChartInsertFretDigitTyped`
  (no selection required — pure pending state); the Alt release reaches the controller as
  `onChartInsertSessionEnded` via `EditorView::modifierKeysChanged`, bounding the run
  accumulation; the ghost's hover position stays view-local (only the fret is
  keyboard-editable state).

## 7. Selection granularity — SETTLED

- **Containment hierarchy (revised 2026-07-17, superseding the chord-unit click):** a single
  click selects the **individual note**, a double click its whole onset group (its chord), a
  click on a span's rails/bracket will select every note in the span (rides the span slice),
  and a double click on the span will open its name/fingering editor (future — the
  "template editor lite" that also serves forced naming and the fuller-shape arpeggio case).
  Each click level selects one containment level up: note ⊂ chord ⊂ span. Rationale for the
  reversal: fret correction on one chord member proved the most common single-note edit and
  three designs (focused member, transpose typing, Ctrl-isolation) were burned working around
  a group-selecting click; cohesion lives at the verb level (verbs act on whatever is
  selected), not in the click. Ctrl+click toggles individual membership; marquee remains
  geometrically precise; inserting selects the placed note (Alt-session accumulation
  unchanged).
- **Right-click context menu deferred (2026-07-17):** every candidate v1 item (Set Fret,
  Insert Note, Delete, Shift) is a strictly worse path to an existing direct gesture. The
  grammar's menu promise stands; the menu lands when techniques give it non-redundant content
  (mute/accent/harmonic toggles). A persistent no-modifier hover ghost was rejected as a lying
  affordance (plain click seeks, it must not advertise insert); if a menu ever needs a
  position marker, the white ring appears only while the menu is open.
- **"Note properties" dialog dropped (2026-07-17):** derived-over-authored leaves no per-note
  metadata needing a form; techniques become selection toggles; chord metadata lives in the
  span dialog; bend curves get a direct-manipulation editor when techniques land.
- **Fret typing = set exact; fret movement = Alt+Shift+wheel (FINAL 2026-07-17,** after two
  superseded designs recorded here for the reasoning trail): slice 1 exposed that fret is
  per-string data. A *focused-member* model (single-member digit targeting, blinking
  underline) was built then rejected — asymmetric (no multi-member retype) and the indicator
  never looked right. A *transpose-typing* model (plain digits move the shape's lowest fret
  to the typed number, Ctrl for exact) was built then rejected — typed-number-differs-from-
  displayed-number would confuse newcomers, and no modifier assignment survived scrutiny
  (Ctrl inverted its precision meaning under the flip; Shift+numpad is unfixable at the JUCE
  level — the Windows NumLock legacy delivers navigation VKs and JUCE discards the lParam
  that could disambiguate). Final: **typed digits set every selected note to the exact value**
  (what you type is what appears; multi-digit window unchanged; Ctrl+digit unbound; Alt+digit
  reserved for the ghost quasimode) and **Alt+Shift+wheel shifts the selection's frets ±1 per
  tick**, shape-preserving by construction, refusing (never clamping) when the lowest fret
  would pass zero or the highest the cap. The relative operation lives on the naturally
  relative gesture; no second selection concept, no focus indicator.
- Shift+click creates a **time-range selection object** (Guitar Pro-style): one big timespan
  highlight, **replace** semantics, anchored at the last non-Shift selection action;
  Shift+clicks while held re-extend from that anchor; with no prior anchor the first
  Shift+click acts as a plain click. This decision transfers to plan 52 (it settles the
  creation gesture and display); what operations do with the range remains plan 52's open
  agenda.

## 8. Arpeggio conversion — SETTLED

- A hotkey converts an in-line placed note into an unplayed shape member: it adds the
  string/fret to the span's template without adding a played note. Under template-relative
  classification this flips the span to arpeggio automatically, and the existing posture
  rendering (unsounded template members) displays it. This resolves the previously tabled
  "display a fuller shape than the notes play" case without a dedicated template editor.

## 9. The caret model — SETTLED (2026-07-17, evening; the Guitar Pro editing posture)

Signed after hands-on Guitar Pro comparison. One position concept per transport state: while
**paused**, the caret (grid position × string) is THE position — where editing happens, where
typing inserts, where play starts; while **playing**, the playhead is THE position (the moving
indicator). They are never visible together, which resolves the objection that killed the
first caret. The paused playhead is gone.

- **Caret existence:** always present once a chart is displayed (song start, string 1, on
  load). Esc clears the note selection; the caret persists. On an empty grid slot it renders
  as a white circle (the old white-ring ghost look); on a note, the note's selection
  highlight IS the caret display.
- **Placement:** clicking the highway band (the waveform/tab lane) places the caret at the
  snapped position (Ctrl bypasses to the fine grid) on the clicked string lane, deselecting
  notes; clicking a note selects it AND co-locates the caret (selection and caret are always
  co-located). Clicks in the tone strip or automation lanes do NOT move the caret or seek —
  the highway is the caret's only pointer surface (plus the ruler, which keeps positioning
  the timeline). Ruler clicks position the caret at the snapped time on the remembered
  string.
- **Typing inserts:** digits on an empty caret insert a note there with the typed fret; the
  multi-digit window continues onto the just-inserted note (2 then 3 → one insert of fret 23,
  ONE undo entry — the widened entry stays an insert, never degrading to a retype that would
  strand the note under undo). Digits with a note selection retype it (unchanged). The
  "plain keys never mutate" rule is formally retired as the grammar's foundation — the caret
  model supplies the deliberateness (typing visibly lands at the caret); the drag-threshold
  and Alt-gate rationales stand on their own for the verbs that keep them.
- **Arrows are caret movement:** plain Left/Right step the caret along the grid on its
  string; plain Up/Down move it across strings; **Ctrl+Left/Right jump to the
  previous/next measure** (Guitar Pro's jump, replacing the fine step — fine positioning
  remains available via Ctrl+click). Arrow movement re-derives the selection from what sits
  under the caret: a note → selected; empty → selection empty, white circle. Alt+arrows
  still MOVE the selection; Alt+Shift keeps the axis rule (horizontal extent, vertical fret).
- **Shift+arrows = caret-anchored time selection** (fills the reserved slot; settles plan
  52's keyboard creation gesture): holding Shift and arrowing extends a time selection
  outward from the caret's starting grid line, text-editor style. Releasing Shift keeps the
  selection and remembers the caret; a plain arrow afterwards clears the selection and
  continues moving; pressing Shift again resumes extending. Builds when plan 52's range
  object lands.
- **Transport:** Space plays FROM THE CARET. On pause, the playhead hides and the caret
  snaps to the nearest grid line to the stop position, on the remembered string. The
  playhead renders only while playing.
- **Deleted by this model:** the entire Alt insert quasimode — the colored composable ghost,
  Alt+digit fret composition (`onChartInsertFretDigitTyped`), the insert-session
  accumulation (`onChartInsertSessionEnded`), `ChartEditViewState::insert_fret`, the
  CopyingCursor, the ghost repaint strips, and sticky last-fret (inserts carry exactly the
  typed digits, nothing else). Alt returns to being purely the mutation gate for
  move/duration/fret-shift. (The full-note ghost shipped hours before this settlement;
  superseded knowingly — the caret workflow is less machinery and faster entry.)
- **Unaffected:** the containment click hierarchy on notes, Ctrl toggle, marquee, Shift+click
  time range (now caret-anchored, matching GP), all Alt / Alt+Shift verbs, delete, undo,
  zoom, the selection-verbs-follow-the-selection rule.

## Build order (once section 5 settles)

1. Selection granularity — chord-unit click + Ctrl precision (editor-core).
2. Classification v2 + universal chord boxes — both projections; importer-normalization
   companion task in the converter tool.
3. Auto-span lifecycle + the duration verb — span creation in planners, split/merge,
   wheel-tick coalescing (same replaceTop pattern as fret typing), tail-visibility rules.
4. Ghost rework — controller-owned composable ghost.
5. Shift+click time range — recorded in plan 52; built when 52's operation semantics get
   their sign-offs.

The Phase 4 remainder (pointer drag-move of selected notes + Esc drag-cancel) slots after
slice 1.
