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

## 5. Span duration, sustains, and the wheel — OPEN (under active discussion)

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

Candidate unified model under discussion (not yet signed):

- Two duration modes per span. **Implied-hold** (default): no member carries an explicit
  sustain; span extent is the authored hold; no tails drawn. **Explicit-tails**: at least one
  member carries an explicit sustain; all tails drawn; span end derives as the earliest
  shape-break (per string, that string's last hold end; minimum across strings). Uniform
  explicit tails equal to the span normalize back to implied on apply (save==publish
  normalization precedent).
- One targeting rule for the wheel in both span kinds: selection covering **all** sounded
  notes of the span → adjust span extent; a **proper subset** → adjust those members' tails
  (materializing explicit sustains). A chord's single click selects its whole group, so
  chords get span adjustment for free; arpeggios need a whole-span selection affordance.
- Shrink-split shared primitive: early termination (from span shrink OR subset release)
  splits later-onset members into a new same-template span.

Open questions (see discussion): arpeggio whole-span selection affordance; confirmation of
termination-plus-split for arpeggio subset shrink; technique-bearing members and mixed
implied/explicit spans; stored-vs-derived span extent representation.

## 6. Ghost note rework — SETTLED

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

## 7. Selection granularity — SETTLED

- Plain click selects the whole onset group (chords are one cohesive unit); Ctrl+click
  selects/toggles individual notes; marquee remains geometrically precise by design (the
  deliberate tool for selecting parts of chords).
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
