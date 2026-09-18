# Harmonic display follow-ups — the 2026-09-18 sighting

Status: **OPENED 2026-09-18** from the user's sighting of the harmonic display work
(`e9d6d640..19a4c475`), against the showcase fixture
`C:\__MAIN__\Coding\__scratch__\rockhero-showcase\technique-showcase.rhp`. Every numbered item below
is the USER's ruling unless it is marked **OPEN** or **WATCH**; nothing here is built. Each item
states the ruling, what the code does TODAY (verified against the tree at `19a4c475`, with
file:line), what changes, and what to verify.

## Why

The five commits that shipped the pressed-stop harmonic — `e9d6d640..19a4c475` — settled what the
CHART says about a harmonic standing over a stop the fretting hand presses: the head prints the
node, the pressed stop is stated beside it (the 2D satellite, the highway's stop→node floor run),
the grip statement is that pressed stop and never the finger a pull-off derives beneath it, and a
pull-off from such a harmonic closes the span. Sighting the result in the showcase package surfaced
a second layer the rulings never reached: what the DISPLAY owes around that satellite, what the
highway owes for the picking hand, and two span-law defects the fixture exposes but the corpus does
not carry.

They are recorded together because they share one subject and two of them share one root — the
claim/hand-table seam. Splitting them across the backlog would lose that, and none of them is a
one-line fix.

---

## 1. A satellite always comes with a bracket — **RULED**

**The ruling.** Wherever a held-stop satellite is displayed, a BRACKET (an arpeggio span) is
displayed with it, for the duration the grip is held. So an artificial harmonic and a tapped
harmonic over a pressed stop ALWAYS display with bracket notation — the pressed stop being the grip
— except where the stop under the node is 0 or the capo, which is the natural harmonic and states no
fretting-hand stop at all. Where a tap already stands over a bracket carrying a satellite, the
satellite looks exactly as it does today; nothing about the satellite's own ink changes here.

**Today.** A lone harmonic over a pressed stop founds NO span, in either form:

- The founding disjunction is `own >= g_span_member_threshold || total >=
  g_accumulation_member_minimum` (`rock-hero-common/core/src/chart/chart_shapes.cpp:1515-1516`),
  with the two thresholds at `chart_shapes.cpp:36` (2 statements) and `chart_shapes.cpp:41` (3
  overlapping sounds).
- An ARTIFICIAL harmonic reaches the slot as one fretting-hand strike, so `grip_statement_of`
  (`chart_shapes.cpp:929`) yields its pressed stop — `gripStatement` returns empty under
  `harmonicOverPressedStop` and the fall-through hands back `slot.strikes`, which `statedStopFrom`'s
  node arm resolves through `frettingStopAt` (`chart_shapes.cpp:137`) to the PRESSED fret. That is
  `own == 1`.
- A TAPPED harmonic makes no fretting-hand strike at all (`chart_shapes.cpp:803`, the
  right-hand-onset skip), and reaches the slot as a CLAIM instead — `claimedStop` answers its own
  `fret` for a harmonic over a pressed stop
  (`rock-hero-common/core/include/rock_hero/common/core/chart/chart.h:984`). Claims are counted at
  `chart_shapes.cpp:1442` (`own += slot.claims.size()`), which is again `own == 1`.

One is below the threshold of two either way, so the fixture's **31:3** derives no span and prints
no bracket, while its head still prints a node and its satellite still prints the pressed stop. That
is the display hole the ruling closes: a satellite with nothing to hang from.

**What changes.** A pressed-stop harmonic's grip statement must FOUND a span on its own, and that
span must run for as long as the grip is held — the harmonic's own ring, and onward through
following notes that hold the same grip, closing by the ordinary grip laws (a contradiction, or the
statement no longer standing).

**OPEN for the law session — the exact founding rule.** The display outcome above is RULED; how the
member arithmetic expresses it is not, because the code admits more than one reading:

- (a) **The claim counts as two.** The harmonic states two facts at one slot — the pressed stop the
  fretting hand holds and the node the other hand touches — so the figure is already a grip by the
  existing rule 4 and nothing about the threshold moves. Attractive because it deletes a special
  case rather than adding one, but it makes "member" mean two things.
- (b) **A pressed-stop harmonic founds at one.** A named exemption to the threshold, keyed on
  `harmonicOverPressedStop` at the founding site. Smallest change, but a second founding rule.
- (c) **The threshold is the wrong question.** A satellite is published per note
  (`rock-hero-common/core/src/chart/chart_projection.cpp:281`, the `StopMarkSlot::Satellite` arm),
  and the ruling ties bracket to satellite — so the span could be founded by the SATELLITE's
  existence rather than by counting stops. Largest change and the only one that makes the ruling
  structurally true rather than arithmetically reproduced.

Settle this in the law session, not in the implementing commit. Whichever wins must also state the
span's EXTENT explicitly: the ruling says "for the duration the grip is held", which is the
harmonic's ring plus every later note holding the same grip — not the harmonic's onset alone.

**Verify.** The fixture at 31:3 draws a bracket under its satellite; the artificial and the tapped
forms of the same figure derive the same span (they already agree on the grip statement since
`19a4c475`); `test_chart_shapes.cpp` gains the lone-harmonic founding case; the corpus census shows
zero change (no corpus package carries a harmonic over a pressed stop).

---

## 2. Satellite background — **RULED**

**The ruling.** Where a satellite does NOT sit over a note tail, its background is the 2D lane
background, not the tail fill colour.

**Today.** `drawSatelliteDigit` (`rock-hero-common/ui/src/tab/tab_paint_core.cpp:1983`) paints white
on an opaque knockout of the string's tail ink, unconditionally:

```
g.setColour(style[Ink::Tail]);
g.fillRect(bar_right, patch_top, slot.extent(), patch_bottom - patch_top);
```

The knockout is deliberate and its rationale is on the function (`tab_paint_core.cpp:1969-1982`): a
known ground is what lets the digit be plain white on all six strings, since the per-string inks do
not read on their own. What it does not ask is whether a tail is actually there to knock out — so a
satellite standing over bare lane paints a stretch of tail-coloured ribbon that nothing is ringing.

**What changes.** The ground becomes conditional on what the slot actually stands over: tail ink
where a tail runs through it, lane background where none does. The white digit stays white — the
rationale for a known ground is unchanged, and both grounds are known.

**What to watch while building it.** The function is shared by both marks that print a satellite —
the span's displaced posture digit and a note's own held face (`tab_paint_core.cpp:2678` and
`tab_paint_core.cpp:2735`) — and that sharing is the point: the two must not differ. Whatever
decides "is a tail here" has to be one authority both call sites hand in, not a second reading
inside the drawer.

**Verify.** Sight the showcase fixture at each satellite the package carries, over a tail and off
one; the existing `common/ui` suite.

---

## 3. Pull-off from an artificial harmonic — **RULED**

**The ruling.** The fixture's **measure 74** figure needs the bracket showing the held 5 in the grip
on the harmonic's string. That follows from item 1 and is not a separate law: the harmonic states
its pressed stop as the grip, so the span exists, and the bracket prints that stop.

**Today.** `19a4c475` made the pull-off close the span correctly — the hold-under arms ask
`gripStatement` rather than the bare plant (`chart_shapes.cpp:895-907`), so a landing on the finger
beneath a pressed-stop harmonic is a new statement and the span closes at the release with the
pressed fret in its bracket. What is missing is the span BEFORE that close, for exactly the reason
in item 1: the harmonic founds nothing alone, so there is no bracket for the close to end.

**What changes.** Nothing beyond item 1. This item exists as the fixture's acceptance case, so the
founding rule is sighted against the figure the user actually looked at rather than only against a
unit test.

**Verify.** Measure 74 of the showcase package draws one span over the harmonic's ring with 5 in the
bracket, closing at the pull-off.

---

## 4. One span across a plain tap pulled off to its own planted fret — **RULED**

**The ruling.** The fixture's **measure 76** — a plain two-hand tap with a planted finger, pulled
off to that same planted fret — must derive ONE span. The grip does not change: the finger that was
waiting under the tap is the finger the release lands on, so nothing about the hand moved and there
is nothing for a boundary to mark. Today it derives TWO (`19a4c475`'s probe: a span at 76:1 and a
second at 76:2).

**Today.** The root is the claim/hand-table seam, pointing the opposite way from item 8. A tap never
becomes the string's finger in the walk's hand table:

```
if (rightHandOnset(member.attack))
{
    continue;
}
string_hand.finger = note_at;
```

(`rock-hero-common/core/src/chart/chart_shapes.cpp:1631-1635`, step 6, "apply the slot to the hand
table".) Two consequences fall out of that one skip at the release's slot:

- `planted_under` (`chart_shapes.cpp:902`) requires `hand[string_index].finger`, so the hold-under
  law's "here" arm cannot see the tap's own plant. The plant is real and recorded in the wide table
  (`chartPlantedStops`), but the bridge that would let the release ride the standing statement never
  finds it.
- `sounded` (`chart_shapes.cpp:955-958`) requires the same finger, so the held side of THE TIE
  DOCTRINE (`chart_shapes.cpp:994-1017`) is empty and the release begins its own statement at its
  own onset instead of inheriting the tap's. A statement that begins here is what dates a new front.

So the release at the planted fret is read as a new statement rather than as the same hand releasing
a finger, and the figure splits at 76:2.

**What changes.** The release must inherit the statement the tap's plant already made. The clean
form is the same one item 8 needs and is stated there: a right-hand onset's CLAIM enters the hand
table, so the string's finger under a tap is the tap, and both the plant bridge and the tie doctrine
see it. Do NOT add a second bridge keyed on claims beside the finger one — that is the same rule
stated twice, and the two would drift.

**Verify.** Measure 76 of the showcase package derives one span; `test_chart_shapes.cpp` gains the
tap-plant-pull-off-to-the-same-fret case; the census is unchanged.

---

## 5. The artificial harmonic's node shows the picking hand — **RULED**

**The ruling.** On the highway, the NODE of an artificial harmonic displays with the right-hand
position cue that taps get — the tap light / right-hand marker. The picking hand touches that node,
so the cue is simply true there. Today artificial harmonics "look really odd" on the board for the
lack of it.

**Today.** The board draws the harmonic's stop→node floor run and nothing about which hand makes it:

- `harmonicMarkFootprint` (`rock-hero-common/ui/src/highway/highway_renderer.cpp:753`) spans the run
  from the fretting hand's stop-slot midpoint to the node, keyed on `harmonicOverPressedStop`, and
  collapses onto the node for a natural. It is pushed as the note's floor line at
  `highway_renderer.cpp:5065`.
- The tap light comes from a separate derived stream. `makeHighwayTapOnsets`
  (`rock-hero-common/core/include/rock_hero/common/core/highway/highway_view_state.h:601`) fills
  `HighwayViewState::tap_onsets` with one entry per onset group carrying a right-hand note, and its
  member filter is `if (!rightHandOnset(note.attack) || sounding_fret <= 0) { continue; }`
  (`highway_view_state.h:686`). `rightHandOnset` is `Tap || isScrape`
  (`.../core/chart/chart.h:439`), so an ARTIFICIAL harmonic — a picked onset carrying a node —
  contributes no tap onset at all and the renderer's light passes (`highway_renderer.cpp:5518`,
  `:5549`, `:5882`, `:6167`) never see it.

A TAPPED harmonic already gets the light, and gets it at its node rather than its stop: the
projection seeds and steps the light through `highwayDrawnStop(note, ...)`, whose comment states the
rule ("a tap harmonic strikes its node"). So the machinery for "light at the node" exists and is
correct; only the artificial family is excluded from feeding it.

**What changes.** The artificial harmonic's node must reach the picking-hand stream. The decision to
take is WHERE, and widening `rightHandOnset` is the wrong answer: that predicate means "the picking
hand PRODUCES THE ONSET at the neck" and is read by the fret-hand generator, posture derivation,
chord grouping and camera framing (`chart.h:429-442`), all of which are correct to exclude an
artificial harmonic — its onset is an ordinary pluck and its stop is the fretting hand's. What the
light needs is the narrower, different question "the picking hand TOUCHES the neck here", true for a
tap, a scrape, and any harmonic whose node the picking hand damps on the fretboard. Note that
`nodeIsOnNeck` (`chart.h:405`) already answers half of it. Whatever it becomes must be stated ONCE
and read by the projection, not spelled at the filter.

Note the scope boundary: this changes only which notes FEED the light. Where the light sits, how it
ramps, and how it travels are already answered by the node-riding rule above.

**Verify.** Sight the showcase fixture's artificial harmonics on the highway beside its tapped ones
— the two should read as the same gesture at the node; `test_highway_projection.cpp` gains the
artificial-harmonic onset case; no change for naturals or pinches.

---

## 6. Editing the pressed stop under an artificial or tapped harmonic — **OPEN**

**The hole the user sees.** The satellite beside such a head is READ-ONLY. It prints the pressed
stop — the one number a charter would reach for to change where the harmonic is played — and typing
at it does nothing.

**Today.** The stop IS editable, just not there. `planRetypeFrets`
(`rock-hero-editor/core/src/chart/chart_edits.cpp:914`) splits by channel:

- On the **Sounding** channel it refuses only a `fretHandHarmonic` (`chart_edits.cpp:1038-1049`) —
  and `fretHandHarmonic` requires `fret == 0` (`.../core/chart/chart.h:1553`), which a harmonic over
  a PRESSED stop is not. So such a note is addressed, and the write carries the node with the stop:

  ```
  // A NODE TRAVELS WITH ITS STOP...
  node = *node + static_cast<double>(value - stop.value);
  retyped.fret = value;
  ```

  (`chart_edits.cpp:1122-1136`.) Typing a digit on the HEAD therefore already re-stops the harmonic
  correctly, node and all, with the finalize gate judging legality.
- On the **Held** channel it refuses outright: `if (!pickingHandStopsString(note.attack,
  note.harmonic_node)) return std::unexpected{ChartPlanRefusal::Invalid};`
  (`chart_edits.cpp:1023-1026`). That is correct as a statement about the FIELD — a harmonic has no
  planted finger to author, the plant beneath it being derived from the pull-off that follows — but
  from the charter's seat it reads as "the number I am pointing at cannot be typed".

So there is no authoring hole in capability, only in DISCOVERABILITY: the place the charter points
at is not the place the verb lives.

**Options to rule.**

- **(a) Route the satellite's digit to the Sounding channel for such a note.** The charter types at
  the number they see and the right thing happens; one place to type the stop. It costs a
  channel-selection rule that depends on the note, which is a second thing the channel choice means.
- **(b) Document the head as the place, and leave the satellite read-only.** Zero code, and it keeps
  the channel meaning exactly one thing — the satellite states a DERIVED fact and derived facts are
  not typed, which is the rule every other derived mark already follows. It costs the charter a
  discovery they will not make unaided.

**Recommendation: (a)**, because the satellite under a pressed-stop harmonic is not a derived fact
about a neighbouring note — it is that note's OWN stored `fret`, printed in a second place because
the head is busy printing the node. Refusing to type the note's own field at the only place it is
legible is the surface lying about what is editable. (b) is the honest fallback if the channel rule
turns out to be the thing that cannot bend.

A third possibility exists and is NOT recommended: **(c) the (unbuilt) pressed-stop verb owns it**.
That defers the question to a surface nobody has designed, and the stop is already typeable today —
so (c) buys nothing (a) does not, later.

Settle with the authoring discussion, not before. See
`docs/plans/todo/artificial-harmonic-authoring.md`.

---

## 7. Sliding the pressed stop under a harmonic — **WATCH**

Physically the fretting hand can slide the pressed stop while the harmonic rings, and the chart
model already follows it: `soundingStopAt` derives the sounding place as `nodeStop(*harmonic_node +
(fret_at_point - note_fret))`
(`rock-hero-common/core/include/rock_hero/common/core/chart/chart.h:1639`), so the node rides the
stop by construction — fret spacing is logarithmic, so the node's offset above the stop is constant
in fret units.

What would be wrong is the DRAWING: a slide tail on the node reads as the picking hand sliding, when
the hand that moves is the LEFT one. No known song does this and no corpus package carries it.

Recorded as a watch item in `docs/tracking/watch-items.md` rather than as work here — **trigger:** a
corpus or authored figure with a fret keyframe on a harmonic over a pressed stop.

---

## 8. Stale finger under a tapped harmonic — **DEFECT**

Moved here from `docs/tracking/backlog.md`, where `19a4c475` recorded it; the backlog keeps a
pointer. It is a defect with a ruling to obtain, not a small fix, and it shares item 4's root.

**What happens.** The fixture's **measure 75**. A tapped harmonic makes no fretting-hand strike, so
`hand[string].finger` (`chart_shapes.cpp:1631-1635`, the same skip item 4 cites) keeps whatever last
PRESSED that string, while the tap's own sound renews the string's reach. The walk then goes on
reading that older finger's stop as what the string holds. Where that stop happens to be the fret
the release lands on, the release reads as the stop already standing, the span does NOT break, the
posture ends as the released fret, and the bracket prints it beside the head's standing pressed stop
— which is exactly the two-digit column the 2026-09-18 grip-statement ruling removed.

**Measured 2026-09-18**, in `test_chart_shapes.cpp` terms: the co-struck tapped figure ALONE splits
in two; the same figure preceded by one plain note on its string at the release's fret (3) derives
ONE span with posture 3; at another fret (7) it splits again; the artificial twin splits under
either prefix. The showcase package carries it at the (f2) measure, right after an (f1) measure that
leaves a finger on 3.

**Ruling to obtain (recommended).** A tapped harmonic's CLAIM updates the hand table as a press does
— the string's finger becomes the claiming note. That is the same change item 4 needs, from the
other direction: item 4 fails because a tap's plant is invisible to the bridge, item 8 fails because
a stale finger is visible when it should not be. One rule fixes both, and it deletes the asymmetry
rather than adding a case. The design question it opens — what exactly a claim does to the hand
table, and what a claim's `covers` should be when no fretting-hand ring backs it — is why this is a
ruling and not a patch.

Zero occurrences corpus-wide (the 2026-09-18 census counted no harmonic over a pressed stop that is
a pull-off source), so it is a correctness question, not a sighting emergency.

The seam itself is the subject of the watch item RETIRED on 2026-09-18 — the retirement closed the
GRIP half by ruling, and this item plus item 4 are the evidence that the HAND TABLE half was never
closed with it.

Related and still in the backlog, deliberately: the importer's `gripStatementAt`
(`rock-hero-editor/core/src/project/gp_chart_builder.cpp:1889`) is a second spelling of the
grip-statement law that did not take the 2026-09-18 harmonic clause. That one IS a small fix.

---

## Order of work (proposed)

1. **8 and 4** — the two span-law defects, which are one change to the claim/hand-table seam. Small,
   and they must land before anything sights a span over a harmonic.
2. **1, then 3** — the founding rule, once the law session has picked (a)/(b)/(c). Item 3 is item
   1's acceptance case on the fixture.
3. **2** — the satellite ground.
4. **5** — the highway's picking-hand cue at the node.
5. **6** — after the authoring discussion.
6. **7** stays a watch item and is not scheduled.

## Verification

- **The showcase fixture, figure by figure, by measure**: 31:3 (item 1), 74 (item 3), 75 (item 8),
  76 (item 4), plus every satellite the package carries on and off a tail (item 2) and its
  artificial harmonics on the board beside its tapped ones (item 5).
- **The corpus census** — ZERO change expected throughout: the corpus carries no harmonic over a
  pressed stop, which is why every item here is a correctness question rather than a regression
  risk. A census delta on any of these items means the change reached further than its ruling.
- **The existing suites**: `test_chart_shapes.cpp` and `test_chart_projection.cpp` for items 1, 3, 4
  and 8; `test_highway_projection.cpp` for item 5; the `common/ui` suite for item 2.

## Relation to other records

- `docs/plans/todo/artificial-harmonic-authoring.md` — item 6's authoring discussion belongs there;
  this plan records only what exists today and the options.
- `docs/plans/roadmap/60-hand-markers.md` — owns the span laws as a whole (the hand marker,
  span-free zones, grip templates). Items 1, 4 and 8 change span DERIVATION under laws that plan
  sequences, so the founding rule ruled here must not contradict 60-Q1..Q5; if it would, it is a
  law-session item, not a follow-up.
- `docs/plans/in-progress/span-derivation-ground-up.md` — the law `chart_shapes.cpp` implements and
  the referent for every rule number cited above. Items 1, 4 and 8 amend it.
- `docs/plans/in-progress/chart-ruleset.md` — the reconciled ruleset; item 1's founding rule is a
  law change it must absorb.
- `docs/tracking/watch-items.md` — item 7 lives there, under *Chart editing (tab lane)*. The
  claim/hand-table seam items 4 and 8 share is the subject of the item RETIRED there on 2026-09-18
  ("A pressed-stop harmonic's claim column and its grip disagree about the planted finger"): that
  retirement closed the GRIP half by ruling, and what these two items found is that the HAND TABLE
  half was never closed with it. Read the retired entry before ruling item 8 — it records why the
  wide planted table stays true and which readers narrow.
- `docs/tracking/backlog.md` — item 8 moved out of it; the importer's `gripStatementAt` clause and
  the plain tap's missing 3D plant cue stay there.
