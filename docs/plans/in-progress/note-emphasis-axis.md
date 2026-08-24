# Note Emphasis Axis — ghost notes, and accent's generalization

Status: **PARTLY BUILT 2026-08-15**, and moved here from `todo/` because it is now half-executed
with live remaining items. Checklist items 1, 2 and 3 are shipped: `NoteEmphasis` replaces the
`accent` bool through the format, both projections, and both surfaces; the document writes
`"emphasis"` and refuses the old key loudly; the Guitar Pro importer maps `AntiAccent` to `Ghost`
and both loud tiers to `Accent`. Item 4's GHOST half is signed on both surfaces and its
alternatives are ripped out; its ACCENT half SIGNED 2026-08-18 as the rendered `medium flat`
light (see `highway-note-art-state.md`). What remains is item 5's editing verb, item 6's
detection touchpoint, and item 7's re-import.

**Item 7 has a second producer nobody had counted.** The external converter tool under
`custom-song-importer/tools/` writes these same chart documents and still emits `"accent": true`,
so every package it produces now fails to load at the first accented note — and re-running it
reproduces the refused key, which makes the tripwire's "re-import the package" advice untrue on
that path. That tool was updated for the PREVIOUS tripwire (it already writes `harmonicNode`), so
this is the same touchpoint missed a second time. Fix it before re-importing anything it produced.

Two findings from building it, recorded because they were not obvious from the design:

- **Guitar Pro's ghost is a sibling element, not another accent bit.** `<AntiAccent>Normal</...>`
  sits beside `<Accent>`, so the two are independently settable in the source file even though our
  axis makes them exclusive. The parse model resolves them where it reads them — beside that same
  field's existing interpretation, which already drops the staccato bit and folds two loud tiers
  together — with the louder claim winning, because a hit drawn quiet invites under-playing it
  where the reverse merely over-plays. Nothing in the corpus exercises that tie-break: across
  15,245 notes, 104 accents and 160 ghosts, not one carried both.
- **The 2D ghost has no conflict with the Alt pending-entry head** (user's observation): that
  preview is an empty circle, so transparency remains free to mean "ghost" in the lane.

## The decision

Replace `ChartNote::accent` (bool) with a three-value emphasis axis:

```
enum class NoteEmphasis { Ghost, Normal, Accent };
```

`Normal` is the implied default and **never serializes** — the wire format writes
`"emphasis": "accent"` or `"emphasis": "ghost"` and omits the field entirely for normal notes, so
the common case costs nothing. The `"accent": true` key is removed in place (no migration, per the
standing rule) with a **loud old-key tripwire** exactly like the `harmonic`/`touch` removal got:
silently ignoring the old key would strip every accent from every saved package.

**Why an enum and not a second bool:** the user's own observation is the hardening — ghost and
accent are mutually exclusive *by construction* in an enum, a matrix cell that never needs a rule.
This is the "illegal states can't exist" style the technique work standardized on.

**Why three values and not the five-tier sketch** ({heavy, accent, normal, soft, ghost}):

- **`soft` dropped, `ghost` kept** — the user's own lean. Guitar Pro has exactly one quiet tier
  (the ghost note) to import from, and note detection argues against a second: two quiet tiers
  must be *distinguished* by the detector, which is strictly harder than detecting one.
- **`heavy` deferred** — the enum extends without disturbing anything if it ever arrives.
  **Import ruling (user, 2026-08-09): Guitar Pro heavy accents import as regular accents for
  now**, with a code comment at the mapping site that heavy accents may be supported later. The
  importer work should also settle what our parser actually receives: GP8 notates two accent
  tiers, and the current single `accent` bool may already be folding them together.

## Compatibility

Emphasis inherits H3's closed form: **compatible with everything**, scrapes included (walkthrough
D4 — an accented scrape is an aggressively played one). Ghost is dynamics exactly like accent, so
no new matrix cells open; the only impossible combination (ghost + accent) is structural.

## Implementation checklist

1. ~~**Format:**~~ **SHIPPED.** `NoteEmphasis` in `chart.h`; the writer emits `"emphasis"` through
   an exhaustive switch (normal omitted, and a value added later cannot serialize as nothing); the
   reader **refuses the old `"accent"` key loudly** — a temporary tripwire, deleted after the
   corpus re-import. Chart rules needed no new checks, exactly as predicted. Two hardenings the
   plan did not anticipate: `isAccented` is the one classifier, so no consumer compares against
   `Accent` by hand, and `Normal` is the enum's ZERO value so value-initialization cannot land on
   the quiet extreme.
2. ~~**Importer:**~~ **SHIPPED.** GP ghost notes → `Ghost`, accents *and* heavy accents →
   `Accent`, with the may-support-heavy-later note. The parser did drop ghost data silently, as
   suspected: it never read the element at all.
3. ~~**Projections/views:**~~ **SHIPPED.** Both view types carry the emphasis value; the D4 scrape
   pass-through carried over unchanged.
4. **Rendering:** **GHOST is currently `half light` on both surfaces. The ACCENT light SIGNED
   2026-08-18 as the rendered `medium flat` glow.** Ghost draws at 0.5 alpha on both surfaces. The
   highway keeps that translucency over its dark world (`g_ghost_alpha`, and a
   sustain that rises from nothing over a fixed span at its onset so a ghost's ribbon emerges FROM
   the head instead of showing through it). **Amended 2026-08-23:** the 2D lane now keeps every
   normal ink color unchanged and uses 0.5 opacity as the ghost indication on its art. Each note's
   opaque tail, marks and head are flattened before that opacity is applied, so the tail cannot
   show through its own head. Fret numbers and linked-waypoint numbers are overlaid fully opaque;
   fret-number plates and slide fret chips use 0.75 opacity. The former lean evaluation remains
   below.

   The former 2D lean choice was reached by rejecting a JUCE transparency layer. The reasons are
   worth keeping because they are properties of this lane rather than of that API: a translucent
   head reveals its own sustain ribbon, the lane line and a chord box's fill through itself (15–95
   luma counts on the strings, measured); un-revealing it needs a knockout, which is a fourth
   restatement of "a head covers its silhouette" and so condemns the design; the layer is sized to
   the CLIP rather than to the note, and the tail body is deliberately drawn outside the technique
   clip, so a tight layer is structurally impossible; and the deferred slide/bend label chips are
   drawn after every head, outside any layer, so the loudest floating ink on a ghost would have
   stayed at full strength.

   **Reopened, sighted and SIGNED 2026-08-15.** The user asked to try genuine translucency —
   *"translucent feels more CORRECT for ghost notes because ghosts are spooky and kind of see
   through"* — with the semantics specified exactly: render the whole note fully, THEN make that
   rendering translucent. That is group transparency, it was built (a per-note JUCE layer holding
   the note's tail, marks and head together), and it was sighted beside the lean, a hollow head
   and a deeper lean. **The opaque lean won definitively**, and the alternatives are ripped out;
   they are recoverable from git history.

   Why translucency lost, recorded because the reasoning is the surface's and will recur:

   - **Over bare lane the two are the same picture to within about four counts.**
     `lerp(ink, ground, w)` is exactly what alpha computes; they differ only in which ground is
     used, and the lane's real ground sits (4,5,7) from the constant the lean uses.
   - **What differs is everything the group cannot contain**, and the group only holds the note's
     own ink: the lane line, the waveform, the measure grid, a chord box's fill, and a NEIGHBOUR's
     ribbon. That last is not exotic — a sustainless member of a strum under a held shape is
     extended to the span end, so inside a chord shape every strum after the first sits on the
     previous strums' still-running ribbons across the whole head, which group transparency cannot
     touch. Every one of those is brighter than a ghost's own ring.

   The shape that made the ORIGINAL objection go away was **generalizing the one ink authority
   that already existed**. `StringStyle` held the per-string chain while a dozen file-scope greys and raw
   whites held the rest, so nothing could act on ALL of a note's ink. Naming them one `Ink` set
   first made the lean one loop. The current group treatment removes the ghost palette entirely:
   `LaneStyles` stores each string's colors once, and the finished note takes opacity once.
   Authority count in that file remains 1.

   Accent rendering: the 2D glow is unchanged; 3D became a **rendered light** and the atlas ring
   it replaces is retired (cell 3 of the head atlas is now empty), because a mark drawn on a head
   could only ever say "accent" and could not be worn by an open string at all.
5. **Editing verb:** the accent toggle (`A`) becomes a three-state concern — decide the grammar in
   the keymap doc when this executes (likely: `A` toggles Accent, a second key or modifier for
   Ghost; do NOT guess here).
6. **Detection touchpoint:** ghost notes are quiet by definition — record in plan 22's terms how
   the detector should treat them (lower confidence threshold? cosmetic tier?) before scoring ships
   anything emphasis-aware.
7. **Re-import:** rides the corpus re-import already owed (frame/harmonic/scrape changes).
