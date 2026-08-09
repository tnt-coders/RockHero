# Note Emphasis Axis — ghost notes, and accent's generalization

Status: **DEFERRED — design settled 2026-08-09 (technique walkthrough D8), unscheduled.** Re-verify
against the current code before executing; the technique-compatibility doc and the walkthrough doc
record the decisions this plan inherits.

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

1. **Format:** `NoteEmphasis` in `chart.h` replacing the `accent` bool; document writer emits
   `"emphasis"` (normal omitted); reader parses it and **refuses the old `"accent"` key loudly**
   (temporary tripwire, deleted after the corpus re-import). Chart rules need no new checks.
2. **Importer:** map GP ghost notes → `Ghost` (the importer currently drops ghost data silently —
   verify the parser reads the element at all and add it if not); GP accents *and heavy accents* →
   `Accent`, with the may-support-heavy-later comment.
3. **Projections/views:** `accent` bool in the view types becomes the emphasis value; the D4
   scrape pass-through carries over unchanged.
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
