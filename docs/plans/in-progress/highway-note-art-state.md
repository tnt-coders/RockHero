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

**The harmonic icon size iteration — awaiting the user's verdict, uncommitted.**

Working tree holds two modified files:

- `rock-hero-common/ui/resources/textures/notes.png` — cell 14 restored to the icon's
  authoring-resolution bytes (sha `1c563476…`; the committed D atlas is `a9cba048…`).
- `rock-hero-common/ui/src/highway/highway_renderer.cpp` — node heads no longer roll on approach.

Both are built and deployed; the editor shows them on restart. The verdict question is stated
under [Open decisions](#open-decisions) below.

To abandon the icon experiment and keep the rest: `git checkout --
rock-hero-common/ui/resources/textures/notes.png` restores the committed D atlas (the renderer
change is independent and stands on its own).

## Shipped 2026-08-15 (newest first)

| Commit | What |
|---|---|
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

1. **Does the mute-sized harmonic icon ship?** Restoring the icon's original size (the measured
   answer to *"is that how big it was originally?"* — the mutes and the original icon are the same
   height, 32.7 tx, against the 22.0 tx seat scale that shipped) makes the icon, not the diamond,
   bound the head: D's diamond then contributes **no silhouette at all** (points recessed 1.0 tx,
   flats covered by 4.7 tx; 73 diamond texels remain, visible only *through* the icon's annular
   gap), and stacking worsens to ≈ 4.8 tx per side. The diamond identity survives fully in the
   hollow approach outline, which is unaffected.
   **The knob, measured**: the icon binds the union only above ≈ 0.94 of original size (30.8 tx).
   At or below that the diamond bounds it again and stacking returns to D's own ≈ 3.7 tx per side;
   icon scale 0.881 puts the points 1.0 tx proud again. So the middle ground is a real, calibrated
   option if full size hides too much.

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
