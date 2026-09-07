# Arpeggio Posture Display — Options

Status: **SETTLED 2026-08-14.** The table below under "What shipped" records the decision that
closed this option space, not the current rule — the note under it says where the code has moved
on since; everything else here is the option space it was chosen from, kept because the dead ends
are expensive to re-walk and several were rejected for reasons no measurement would have found.

**Two of the rejected options lost their premise on 2026-08-22** (note-sustain-model stage C), and
the record is kept as written rather than rewritten. Hand-posture spans and their postures are now
derived from the notes instead of stored, and a posture carries neither a name nor a fingering —
nothing ever authored either, so `ShapeViewState::name`, `ShapeStringViewState::finger`, the
timeline ruler's name-chip band with `setShapeLabels`, and the highway's fingering panel were all
deleted as branches that could never fire. Option A's name chip and option B's fingering-bearing
diagram would therefore have to bring their own data source back with them: when names and
fingerings are authored they arrive as a dictionary keyed by a posture.

## What shipped

One decision per posture string, at the span start:

| At the span start | Result |
|---|---|
| nothing sounds | the posture states CENTRED in the bracket, at fret-number size |
| a head at the posture's own fret | nothing added; the head already states it |
| a head at a DIFFERENT fret, picking-hand onset | the tap keeps the centre; the posture takes a side chip beside the bracket, on a ground of the tail's own fill |
| a head at a different fret, fretting-hand onset | **SUPERSEDED TWICE — see the note below** |

Technique marks riding a tail — slide diagonals, bend curves, the vibrato sine — CLIP against every
arpeggio bracket's columns on their string (the tail's body and the tremolo teeth show through
untouched), so a centred digit needs no ground of its own: the marks that would cross it die at the
bracket's edge. Accepted-for-now consequence in `docs/tracking/watch-items.md`: a scrape or a slid
tap crossing a bracket has its travel diagonals gapped there.

**That last row is kept only as the 2026-08-14 record; it has been superseded twice.** The SLOT
stopped asking which hand made the head — a centred digit sits exactly where a head sits and the
note pass paints after the brackets, so any head sounding elsewhere covers it and the satellite is
the only slot that survives. THE PLANT'S FACE (user ruling 2026-09-07) then made the hand the
answer to WHO prints the displaced digit rather than to whether one prints at all: under a
fretting-hand head the stop a pull-off PLANTS is the NOTE's own reveal-only satellite and the
bracket prints nothing on that string, while a fretting-hand head holding no plant — an artificial
harmonic — still takes the bracket's standing satellite. The live statement is
`ShapeStringViewState::digit`'s doc block in `chart_view_state.h`, the one authority the painter
and the hit target both read, and it is deliberately not restated here. Rows 1 to 3 still hold,
modulo the 2026-09-06 node-grip refinement that compares PLACES rather than printed numbers.

Two changes to the tail came with it, both of which fixed problems wider than this mark: the tail's
**fill** drops to the linked-note fill (as dark as the keyframe heads riding it) while its **edge**
stays at full brightness, and the tail's right **end cap** is gone. The bracket marks moved from the
note fill to that surviving tail edge, since the fill colour was chosen against a bright tail and
inverts once the tail darkens.

The experiment that produced this — three cyclable axes on `F6`/`F7`/`F9` — is preserved in full in
commit `25f17375` and was removed immediately after.

## The problem, stated once

An arpeggio spreads a chord's notes over time, so the *shape the fretting hand holds* is invisible
in a tab lane — the heads that would show it as a vertical stack are strung out across a beat or
more. 2D has no fret axis (its space is string × time), so it cannot show a posture the way the 3D
highway does, which is spatially: the highway draws arpeggio spans as purple floor rails along the
hand window's fret lines and reuses the chord-box geometry for "arpeggio-styled hand-shape boxes"
(`highway_renderer.cpp:793`, `:1946`, `:1958`), positioned *at the frets* because the highway's
space IS the fretboard. It prints no posture digits anywhere and does not need to.

So 2D must state the posture as **text**, or not state it at all — and not stating it puts 2D
strictly behind 3D on the reading surface, which is the direction the no-surface-divergence law
forbids.

### Why it is hard

Five constraints, and they are over-determined:

1. The mark must read as **belonging to the posture** — the brackets are the posture mark, so a
   number outside them reads as detached (user, live).
2. It must not collide with a **note head** that can land at the bracket's centre — including a tap
   stating a *different* fret, which is the motivating case.
3. It must survive a **sustain ribbon** crossing the row, from either direction.
4. It must work at ~**8 px** cap height, where `fret_font` and `posture_font` both hit their floor.
5. **Six** of them stack per span with ~5.75 px of vertical clearance between lanes.

(1) and (2) can only both hold if the bracket widens, and every widening spends clearance. That is
the whole reason this has taken four rendering rounds.

## What the research actually found (2026-08-14)

- **No product displays per-string posture inline during an arpeggio.** MuseScore's own issue
  tracker describes holding a chord while arpeggiating as "extremely common in guitar playing but
  often sloppily written down using TAB notation without any proper note lengths", with no
  convention offered. The user's read — that this is genuinely new ground — is correct.
- **But the information is not new; its *form* is.** The universal 2D representation of a
  fretting-hand shape is the **chord diagram** (fretboard grid). Guitar Pro ships it as a score
  option (`F7 > page & score format > enable diagrams in the score`). Diagrams carry frets *and*
  fingering, which our `ChordTemplate` already stores (`frets` + `fingers`).
- **Soundslice ships a chord-chart VIEW**: hide the staff and tab in the mixer and only the chord
  diagrams remain; and diagrams may be placed anywhere in a bar, not only over a note. This is
  direct precedent for treating posture as a **toggleable layer** rather than permanent lane ink.
- **Two-hand tapping notation solves a different problem.** Published practice marks *which hand
  plays each sounding note* — `R`/`L` letters stacked below the column, a circled `T` for a
  fretting-hand tap — and never states the held shape's frets. So even the standard leaves our case
  unanswered, which is corroboration rather than a template.
- **Detail-on-demand is the established HCI frame** (Shneiderman: overview first, zoom and filter,
  then details-on-demand). Two families: *selection-based* (hover/click) and *zoom-based* (semantic
  zoom, where the content — not just the scale — changes with zoom level).
- **State-on-change is the lead-sheet convention**: a chord symbol persists until a new one is
  marked. The project already applies exactly this rule to hand positions — floor fret numbers mark
  "a hand position being established … and nothing else" (user rule 2026-07-28).

## The options

Each is stated as its *principle* first, because several compose.

### A — Voicing text on the shape's name chip

`Dm  x-0-3-2-1-x` beside the existing chord-name chip in the timeline ruler's label band, fed from
the same tab projection that already supplies names (`setShapeLabels`).

- **Buys:** the whole posture in one place, in the universal chord-chart idiom; zero lane geometry;
  no collision with heads, ribbons or each other; nothing stacks.
- **Costs:** string↔digit mapping requires counting positions rather than reading along a lane.
- **Known flaw — VERIFIED 2026-08-14, and it is worse than "inherits the suppression".** The ruler
  runs greedy left-to-right overlap suppression for every label row (`RulerRowPlacement`,
  `timeline_ruler.cpp:78-125`): a label occupies its measured width plus `g_label_width_pad{8}`, and
  the next may not start until `g_label_gap{10}` past it. The class's own comment records that "on
  dense maps far more candidates arrive per rebuild than survive suppression."
  So the flaw is **self-defeating, not merely inherited**: a chip carrying `Dm  x-0-3-2-1-x` is
  roughly four times the width of one carrying `Dm`, and a wider chip suppresses proportionally more
  of its neighbours. The more posture the chip carries, the less often it is drawn — and it fails
  first exactly in the dense tapping material this whole case comes from.
  **This does not kill A. It makes C load-bearing for it** (see below).

### B — Miniature chord diagram at the span start

A small fretboard grid, in the label band or floating above the span, instead of text.

- **Buys:** it is *the* idiom for this information, so it needs no learning; it is spatial, which
  means it says the same thing the 3D box says, in the same way; `ChordTemplate` already stores
  fingering, which no other option can show at all.
- **Costs:** needs real vertical space — a diagram is not a chip; the label band would have to grow
  or the diagram would have to float over the lane. Legibility at small sizes is unproven for us.

### C — State on change only

Whatever form is chosen, show it when the shape is **established**, not at every span.

- **Buys:** consecutive spans sharing a posture collapse to one statement. On the material where
  this matters (a held shape under a tapped passage) that is the difference between one mark and
  one per span. Directly house-consistent with the settled floor-number rule.
- **Costs:** a reader who scrolls into the middle of a held passage sees no statement. Mitigated by
  the same thing that mitigates it for chord symbols: the rails show the shape is still in force.
- **Composes with A, B, D, E — and is LOAD-BEARING for A and B.** The ruler's suppression is driven
  by how many label candidates arrive per rebuild. State-on-change collapses a run of spans sharing
  one posture into a single candidate, which is precisely the density that would otherwise suppress
  a widened chip. Without C, a voicing chip is widest exactly where it is least likely to survive;
  with C, the wide chips are rare enough that the greedy walk has room for them. **Do not evaluate A
  or B without C.**

### D — Semantic zoom

Posture ink appears only when the lane is tall enough to carry it — the same shape as the existing
`draw_text` threshold, but with its own (higher) bar.

- **Buys:** density stops being a problem by construction; the dense case is exactly the zoomed-out
  case. It is the established answer to this class of problem.
- **Costs:** the information is unavailable at the zoom where a player often reads. Best as a
  *modifier* on another option, not as the answer.

### E — A posture layer, toggled by a rebindable command

Soundslice's chord-chart view, scoped down: a toggle that shows the posture layer (digits, or
diagrams, or the voicing chip) over the lane; off or on by user preference, persisted.

- **Buys:** full per-string alignment *when wanted* and zero clutter otherwise; no compositional
  compromise, because the layer can use the head's own centre when it is the only thing drawn;
  it is a normal command, so it stays rebindable and needs no exception to the command registry.
- **Costs:** discoverability; and it is a mode, so what you see depends on state.
- **Note:** the user's hold-to-peek variant is this option with a while-held key instead of a
  toggle. **A while-held key cannot be a registry command** (commands fire on press; the
  `keyPressed` decoder that owned key-state was deleted in the 2026-07-21 total-rebindability
  change), so peek would be the one non-rebindable binding in the editor. A toggle avoids that
  entirely. If peek is still wanted later, the ghost vocabulary already exists to render it.

### F — Centred with yield (the simple base)

Digit in the head's own centring box; a head that lands there wins.

- **Buys:** never reads weird, because the digit is where the bracket says it should be; no slot, no
  casing, no widening; least code of any option.
- **Costs:** on a tapped string the number shown is the *tapped* fret sitting in the posture's
  position, so a scanned column reads as a voicing that is not the voicing. A wrong number in the
  right place is worse than none — unless paired with A/B/E, which state the true posture elsewhere.
- **Strongest as a pair**, not alone.

### G — Inside the enclosure: `[ ● 3 ]`

Widen the bracket so the digit sits inside it, beside the head.

- **Buys:** satisfies the enclosure grammar with per-string alignment — the only option that gives
  both.
- **Costs:** measured at +1.0 px to the next head and −0.9 px into its hammer triangle in the
  densest two-digit case at the largest lane. Horizontal spacing does **not** shrink with note
  height, so at shorter lanes there is *more* room, not less — this may be a fixture artifact rather
  than a real constraint at the user's zoom. Untested in the app.
- The left-hand mirror `[ 3 ● ]` reads better still (posture, then the note it produces) but
  overlaps the tap plate by 3.7 px — in the tap case, which is the case this exists for.

### I — Darken the sustain tail (user, 2026-08-14) — attacks the cause, not the symptom

Multiply the tail's fill and edge down from the shipped `base x0.66`. **`F7` cycled 1.0 / 0.75 /
0.55 / 0.40, independently of `F6`** (harness removed 2026-08-14), so any posture candidate could
be sighted against any tail.

- **Buys:** every contrast fight in this document is downstream of one fact — the tail fill is
  bright. On the yellow string the tail measures L\* 58.4 and its EDGE measures L\* 80.8, brighter
  than any sensible ink, which is why a digit crossing it inverts polarity. Darkening fixes the
  whole class at once instead of tuning each mark: posture digits, slide fret labels, bend chips,
  the mute X, the vibrato sine, tremolo gems, and the white-backed chip options all gain.
- **It converges the surfaces**, which no other option here does. The 3D highway's tails are
  already more translucent (user), so this moves 2D toward 3D rather than further from it — the
  direction the no-divergence law wants.
- **Costs:** a tail must still read as a ringing sustain. There is a floor, and it is the thing to
  find. String identity shifts further onto the tail EDGE and the head, which is where it mostly
  lives already.
- **May rescue the simpler candidates.** If a darker tail is enough on its own, the plain centred
  digit — the only reading the user accepted — may need no deferral, no casing and no slot at all.
  **Judge every posture candidate at each tail darkness before ruling on posture placement.**

### H — Drop posture from 2D entirely

Brackets and digits both go; posture lives only in an editing popup.

- **Buys:** maximum quiet; deletes the most contested code in the file.
- **Costs:** 2D becomes strictly poorer than 3D on the reading surface — the divergence direction
  the law forbids — and the game's planned 2D tab view (roadmap 30) has no popup at all. Listed for
  completeness; adopt only as a deliberate ruling that the lane carries less than the highway.

## Recommendation for the morning

**Pair F with A or B, and apply C.** Centred-with-yield in the lane (which never reads wrong
compositionally), plus one authoritative statement of the true voicing per shape *change* in the
label band. That resolves every constraint: no collision (F yields), no ribbon problem (nothing to
protect), no enclosure weirdness (the digit is inside), no density explosion (C), and the tapped
string's true posture is still stated (A/B).

The two things to settle before building are (1) whether the ruler's chip suppression would hide
the voicing on dense maps, and (2) whether A's text or B's diagram reads better in the band.

**Second choice: G alone**, if per-string alignment turns out to matter more than the band. It is
the only single-mark option that satisfies the grammar, and its measured cost may not exist at the
lane size actually in use.

## Test harness — REMOVED 2026-08-14 (recorded for the method, not for use)

Cycling the candidates in the real editor is the right instrument: four rounds of harness
measurement missed both objections that actually mattered (the sustain ribbon, and the enclosure
grammar), because neither is a contrast problem.

`F6` = **Cycle Posture Display (experiment)**, in the Authoring category, so it was rebindable like
anything else. Each press advances one candidate and repaints; nothing is persisted, no undo entry
is written, and the lane returns to the shipped candidate on restart. The cycle deliberately
isolates *causes* as well as candidates:

| # | Candidate | What it isolates |
|---|---|---|
| 1 | Satellite, cased (shipped) | the baseline that was rejected in live use |
| 2 | Satellite, **no casing** | whether the casing is what makes it unreadable |
| 3 | Satellite, **full fret size** | whether the 0.8x size step is what makes it unreadable |
| 4 | **Centred with yield** | the simple base — and makes its one loss visible |
| 5 | **`[ o 3 ]`** — bracket widened right | the digit inside the enclosure |
| 6 | **`[ 3 o ]`** — bracket widened left | the same, in preparation order; watch the tap-plate overlap |
| 7 | **`o [ 3 ]`** — centred, deferred in TIME | keeps the centred reading AND states every posture |
| 8 | **`o [ 3 ]`, minimal** | the same, minus the marks the chart already states |

**Candidates 7 and 8 are the answer to "only centred reads acceptably, but I don't want to lose
information"** (user, live, 2026-08-14). The insight is that the collision sits on the axis with no
room — strings are fixed — while **time has slack**. So the digit keeps the centred-in-brackets
reading that works, and when a head occupies the span start the whole bracket *slides past it* on
the same string: `o [ 3 ]`. The head keeps its own centre, the posture keeps its own enclosure,
nothing is hidden, and no new register, casing, colour or size is introduced. Candidate 8 adds the
only suppression that is honest — a head sounding the posture's OWN fret is a true duplicate, so
that one string's digit is dropped and every non-duplicate still shows.

Options A and B (label-band voicing text, miniature chord diagram) are NOT in the cycle: they live
in the ruler's label band rather than the lane, so they need ruler work rather than a paint switch.
Judge those from the rendered sheets first, and only build one if it wins on paper.

**This is throwaway scaffolding.** It costs a mutable module-level variant in `tab_paint_core.cpp`
(a shape this codebase otherwise avoids, tolerated only because it is an experiment with one
UI-thread writer), an enum and two accessors in the header, a switch in the bracket pass, an
`EditorCommandId`, its registration, its perform arm, and a row in the locked keybind table. Every
one of those is marked TEMPORARY and comes out as a single change once a candidate is chosen.
