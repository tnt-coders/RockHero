# Highway Note Art — the live state of the 3D visual pass

Status: **ACTIVE.** The recovery point for the highway's note-art work: harmonic heads, vibrato
motion, and the 2D tail geometry that moves with them. Opened 2026-08-15 at the user's direction
— *"make sure our task list is continuing to be written somewhere that we can recover it and
continue EXACTLY where we left off if we get disconnected"* — on the same convention
`technique-review-walkthrough.md` already uses for the technique decision queue.

**Two live queues, no overlap.** The technique-decision items (W3–W13) live in that walkthrough's
own LIVE work queue and are NOT restated here; this file carries the visual pass, which runs on a
separate track. When resuming, read both.

## In flight right now

**A deep dive into the atlas's SIZE SYSTEM, with whole-atlas variants to judge as a whole.**
The texture agent is measuring every marker cell (mutes, pick slide, slap, pop, tap, hammer-on,
accent, harmonic, pinch, bend) against the head bases and reporting whether one size system
exists or has drifted, which sizes are load-bearing signals, and which structural constraints a
family-wide shrink could break — then delivering 3–5 complete atlas variants implementing
coherent schemes.

**Scope, ruled by the user 2026-08-15.** SETTLED and not to be resized — the rectangular head
bases (standard, tech, anticipation hollow), the diamond harmonic base and its hollow, and the
arpeggio brackets ("look proper at their current size"). UNDER EVALUATION — the technique
symbols only: accent, legato, tap, pick slide, slap, pop, palm mute, full mute, harmonic, pinch.
The bend chevron is borderline ("seems to look good too, but can be considered"). Because the
head bases are fixed, they are the yardstick: sizes and scheme rules are expressed as ratios to
the settled head rather than as absolute texels.

**The toggle mechanism, and why it is files rather than cells.** The engine loads the head atlas
as a whole file by name, so a sizing scheme is a complete atlas variant: switching is copying a
variant over `rock-hero-common/ui/resources/textures/notes.png` and rebuilding, with NO code
change and the cell vocabulary identical in every variant. Only the winner enters the repo; the
rest are deleted. An earlier per-cell candidate seam for the icon sizes was reverted in favour of
this — it cost atlas cells and code for a switch the file swap does for free.

Working tree holds only `notes.png` (currently the full-size-icon experiment, sha `1c563476…`).
`git checkout -- rock-hero-common/ui/resources/textures/notes.png` restores the committed D
atlas at any time.

## Shipped 2026-08-15 (newest first)

| Commit | What |
|---|---|
| `33e43599` | Node heads hold flat through the approach (no rolling flip) |
| `832558b7` | This state file |
| `ae589b2e` | Locked in the edge-height diamond harmonic base (D); rejected candidates removed |
| `21bfa768` | The A/B/C/D candidate rounds, all four in the atlas behind one alias seam |
| `528c412b` | (superseded by the two above) the E-fit round base and `highwayNodeHead` |
| `41af229e` | Vibrato: rigid approach sampling, eighth-note period, depth halved to 0.125 st |
| `fc686489` | Tail envelope made symmetric; technique marks compress into the interior |

## Decided, with the numbers the decisions rest on

**The harmonic base is D: a diamond whose EDGE equals the regular head's height** (the
head-height square rotated 45°, vertex span ≈ 30.6 tx), chosen 2026-08-15 from four measured
candidates. The marker's ring lands inscribed in it (clears the flats by 0.08 tx) with the points
4.4 tx proud. Accepted price: node heads stacked at the 23.33 lane pitch interpenetrate ≈ 3.7 tx
per side — the user sighted this and accepted it (*"D looks okay stacked even with a bit of
overlap"*). The rejected candidates (halo circle, 26.4-box diamond, diagonal-height diamond) are
recoverable in full at `21bfa768`.

**Atlas layout** (256×320, 4×5, capacity 20): cell 14 the harmonic marker, 16 the diamond base,
17 its hollow twin, 18/19 spare. `g_head_cell_count` is 18.

**One shape law.** The filled base, the landing ring, and the pre-bend outline all select their
silhouette from `highwayNodeHead` (`highway_head_marks.h`), which asks the board's own placement
rule, so the approach can never preview a shape different from the one that lands. The same
predicate now also holds node heads flat through the approach.

**Vibrato**: one wobble per eighth note (quarter-note-referenced through the measure denominator),
depth 0.125 semitones, wave anchored to the note's own extremes so it stays rigid on approach.

## Open decisions

1. **The marker family's sizing — one system or drift, and should it shrink?** The user's standing
   preference on the harmonic marker is the SEAT scale (0.6767, 22.0 tx) over the mute-matched
   full size; that question folded into this larger one rather than being settled alone.
   Measured so far: the mutes and the icon's own authored size are the same height (32.75 vs
   32.70 tx) — so *"as tall as the mutes"* and *"as authored"* are one size, and at it the marker
   bounds the head outright (D's diamond points recessed 1.0 tx, flats covered by 4.7, stacking
   ≈ 4.8 tx per side). **The calibrated knob**: the marker binds the union only above ≈ 0.94 of
   authored size (30.8 tx); at or below, the diamond bounds it again and stacking returns to D's
   ≈ 3.7 tx per side, with scale 0.881 putting the points 1.0 tx proud. Awaiting the deep dive's
   analysis and variants before ruling.

2. **The bend display anchor** — is *half step = exactly one string gap, every string* right?
   The curve SHAPE in `highwayBendLiftY` is verified physics; the anchor is a display choice. For
   the user's stated standard (25.5" scale, .009 set, measured at the 12th fret) true travel is
   per-string: high E ≈ 1.5 gaps, B ≈ 1.15, G ≈ 0.9, wound ≈ 0.9–1.1 — so physical accuracy means
   per-string anchors, and equal-pitch bends would then draw unequal heights. Two couplings before
   changing it: at ≈ 1.5 the three-whole-step ceiling (2.86 gaps → 4.3) outruns a six-lane grid, so
   `highwayBentNoteY`'s saturation guard would clamp LEGAL bends and break its own stated
   guarantee; and vibrato's just-tuned depth re-opens, since drawn swing scales with the anchor.
   Scale length barely matters (24.75" differs ≈ 6%; travel goes as L²); fret position is ±15%;
   real setups need somewhat MORE travel than these figures (stretch behind nut and saddle), so
   they are lower bounds.

## Watching

- **Stacked node heads.** D's overlap is accepted on sight, not measured against the corpus. If it
  reads wrong in real charts, the evidence to gather first is how often simultaneous harmonics land
  on adjacent strings — a corpus count, not a guess.

## Conventions this pass established

- Every filled head cell has a HOLLOW twin derived the way the rectangular anticipation ring
  derives from the standard head; a new base shape needs both or the approach lies about the
  landing.
- Marks carry their seat scale in their own art on one uniform quad. The harmonic marker cannot be
  merged into a base cell: per-quad shader clamping is load-bearing (the family highlight
  overdrives past white and the icon's translucent moat darkens the CLAMPED result — a merged
  structural cell measured 74 counts off).
- Texture work goes through the texture-author agent, which verifies byte-preservation of untouched
  cells and reports measured extents; candidate rounds are committed before the rejects are ripped
  out, so a reversal is a `git show` away.
