# The Unified Waypoint Model — one array of per-channel statements along a note's ring

Status: **DECIDED 2026-08-26, BUILT 2026-08-27** — all four stages landed as local commits
(format+rules `18e8d822`, importer `49c8f768`, projection/renderers `b40427fe`, editor with this
commit), each adversarially reviewed with the frozen-bends contract green throughout. Open
sign-offs below remain open (the trim floor value, the coincident-onset vibrato overwrite, the
refined importer-anchor wording, the disconnect's unstruck-tie default), plus the stage-flagged
rulings recorded in the session task list; the #78 re-export is now due. This plan is the outcome
of the tail-model analysis
(`tail-event-model.md`, kept as the option-space record) and supersedes its deferral: the user
ruled the substrate, then closed the two format gaps the ruling exposed. Written by the
orchestrator directly at the user's direction; every ruling below is dated and quoted or marked
as a proposal.

## The decision trail

1. **The substrate** (user, 2026-08-26): technique-bearing waypoints over interval spans. The
   deciding argument is the user's own: under parallel arrays, a slide waypoint, a vibrato start,
   and a bend point can state the SAME offset three times — "three different places specifying
   the SAME information … exactly the type of duplication we try to avoid" — and although each is
   its own axis's authored anchor, the copies shear under edits: drag the waypoint and the
   coincident vibrato start and bend point stay behind, silently breaking the authored figure
   with no validator able to object (both states are legal; only the intent broke). Waypoints
   store the location ONCE and techniques land on it, so moving the moment moves everything that
   meant "at that moment." This also made the bend study unnecessary as a substrate decider: the
   coupling defect exists in the span model regardless of what bends do, so no bend outcome could
   have flipped the board back.
2. **Bends join the waypoint** (user, 2026-08-26): "when moving waypoints, associated bends would
   need to move with them the same way other techniques do." Following that requirement all the
   way dissolved `bend[]` entirely: a waypoint's bend value interpolates between bend-stating
   points exactly as fret interpolates between fret-stating points — one mechanism, one rule.
3. **Fret becomes optional** (repairing the gap the user found — "a bend may change value
   MID-slide"): `fret` is an integer, so a bend change mid-travel between adjacent frets cannot
   state its true fractional position; requiring a fret would kink the slide to say something
   about the bend. A fret-less waypoint states nothing about position (the slide interpolates
   through it unkinked) while anchoring its bend or vibrato change. Same shape the arpeggio hold
   marker ruled the same week: fret required only where the hand's position is the fact being
   stated.
4. **The name** (user, 2026-08-26): "waypoint reads better." Considered and declined: "stop"
   (misread as stop-playing by the map's own author), "keyframe" (semantically exact — per-channel
   keys on a shared timeline — but imported vocabulary; waypoint does not lie and costs nothing).
5. This **overturns the 2026-07-06 mid-sustain vibrato spans decision** in
   `note-format-and-tablature-plan.md`, which never weighed the coupling argument, waypoint
   selectability, or bend anchoring.

## The model

```
ChartNote:   position, string, fret, sustain, attack, … (onset facts, unchanged)
             bend (onset value; a pre-bend is a nonzero onset bend)
             vibrato (onset state)
             waypoints[]  — replaces slides[]; bend[] and the old whole-note vibrato dissolve
             slide_out    — the unpitched falls-away terminal, fret only (W11: offset deleted,
                            it is the ring's end by definition)

Waypoint:    { offset, fret?, bend?, vibrato? }  — states any SUBSET of its channels; a waypoint
             stating nothing is illegal (the validator refuses it; the editor's dissolve law
             makes it unauthorable)
```

**Per-channel semantics.** Each channel reads independently along the ring:

- **fret** (position, discrete): interpolates between fret-STATING points — equal frets = held,
  different frets = travel (the user's fret-information reading, preserved where stated). A
  fret-less waypoint is pass-through for position.
- **bend** (push, continuous): interpolates between bend-stating points; held flat past the last
  one. Compound bends are sequences of bend values; a bent slide is a bend value held across
  fret-stating points; a mid-hold curl is a waypoint with equal-or-absent fret and a new bend.
- **vibrato** (state): holds from each statement until the next; multiple regions, starts and
  ends mid-hold, and vibrato through travel are all just statements (the user's stress cases,
  all binding: slide→vib→slide→vib; vibrato during a slide, "rare but real"; delayed start;
  mid-hold end).

One reading authority — `ringStateAt(note, offset)` per channel — is the only consumer of the
onset-facts-plus-waypoints split. The onset-as-waypoint-zero retrofit is DECLINED for identity
reasons (selection, hit-testing, transposition, and the legato resolver key on the note's fret);
the recorded trigger to revisit is a third onset/waypoint spelling divergence causing a real bug.

**Admission rule for a new channel:** an interval property of a ringing string that real charts
show changing mid-ring. Tremolo stays whole-note (re-picking; a mid-ring "start" is new onsets).
Palm mute and dead stay off waypoints; a mid-gesture mute change is authored by the split
(`Shift+L` disconnect, W10 addendum).

**Coupling law (the decision's heart):** techniques authored ON a waypoint move with it; a
deliberately mid-travel statement is its own waypoint and correctly does NOT move when its
neighbours do.

## What changes, by layer

1. **Format** (in place, no migrations; the #78 re-export is already forced): `"slides"` →
   `"waypoints"` with per-channel-optional entries; `"bend"` array and boolean `"vibrato"`
   dissolve into waypoint channels + onset fields; `"slideOut"` loses `"offset"`. Documents
   carrying the old keys hit the refusal path with the re-import remedy (the chords/shapes
   convention).
2. **Rules**: per-waypoint normalize/strip arms (the scrape, dead-note, and fret-hand-harmonic
   sheds learn the channels — the cost the user priced and accepted); the empty-waypoint
   refusal; W9-K's bend ≥ 0 validation (ratified 2026-08-25) lands here; ordering rules carry
   over per channel; the presentation trim gains the vibrato-start floor WITH a minimum extent
   (the point-floor/interval-state finding — value to be signed at build).
3. **Importer**: the OR-smear dies; GP bend points and slide waypoints merge into one waypoint
   list (offset rebasing already exists); the anchorless GP vibrato flag takes the PROPOSED
   default — anchored at the last waypoint when slides exist (31 of the corpus's 34
   slide-then-vibrato occurrences arrive through the legato merge), else the onset — awaiting
   sign-off.
4. **Projection/renderers**: the terminal leaves the flattened view list (W9-L); both surfaces
   read the per-channel interpolation through one shared authority. Visual identity at
   migration: the curves drawn from waypoint bend values are the same polylines drawn today —
   imported bends keep working through the whole change, regression-locked.
5. **Editor**: waypoints become selectable (the selection-unit widening shared with the arpeggio
   hold marker); state authoring per the user's described flow under the GENERALIZED dissolve law
   (a pending point dissolves at settle iff it changes neither path nor state); `Shift+L` on a
   waypoint disconnects (W10 addendum; unstruck-tie default still a proposal). Bend AUTHORING
   verbs and the handle display are the follow-on study, not this build.
6. **The display/authoring study** (what remains of the old bend study): how bend-bearing and
   fret-less waypoints draw without full-size-handle clutter in the lane's tight vertical space
   (texture-author renders, ui-design-expert judges), and the `B` verb's design. Storage no
   longer waits on it.

## Open sign-offs, carried

- The importer's vibrato default (item 3 above).
- The trim floor's minimum extent value.
- The unstruck-tie default for the waypoint disconnect.
- W9-F (pitched-vs-falls-away glyph, with W9-D) and W9-G (mute at junctions — the
  inherit-at-junctions/split-to-change frame is the standing candidate) re-enter after the model
  lands.

## Sequencing

After the arpeggio hold verb (#106) and tone baseline anchor (#107) builds, or interleaved at the
user's call; the format touch should ride the same window as the #78 re-export so charts convert
once. Stage the build as format+rules → importer → projection/renderers → editor, each with its
own commit and the bends-regression suite green throughout.
