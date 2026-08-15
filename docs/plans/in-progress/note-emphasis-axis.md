# Note Emphasis Axis — ghost notes, and accent's generalization

Status: **PARTLY BUILT 2026-08-15**, and moved here from `todo/` because it is now half-executed
with live remaining items. Checklist items 1, 2 and 3 are shipped: `NoteEmphasis` replaces the
`accent` bool through the format, both projections, and both surfaces; the document writes
`"emphasis"` and refuses the old key loudly; the Guitar Pro importer maps `AntiAccent` to `Ghost`
and both loud tiers to `Accent`. What remains is item 4's ghost RENDERING (accent and ghost
appearances are being sampled live behind toggles — see `highway-note-art-state.md`), item 5's
editing verb, item 6's detection touchpoint, and item 7's re-import.

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
4. **Rendering:** ghost draws as a **partly transparent note head** (user's design) on both
   surfaces; accent rendering unchanged (2D glow, 3D treatment). The 3D ghost treatment should
   reuse the same transparency idea unless the highway pass finds it illegible.
5. **Editing verb:** the accent toggle (`A`) becomes a three-state concern — decide the grammar in
   the keymap doc when this executes (likely: `A` toggles Accent, a second key or modifier for
   Ghost; do NOT guess here).
6. **Detection touchpoint:** ghost notes are quiet by definition — record in plan 22's terms how
   the detector should treat them (lower confidence threshold? cosmetic tier?) before scoring ships
   anything emphasis-aware.
7. **Re-import:** rides the corpus re-import already owed (frame/harmonic/scrape changes).
