# Tempo Map Format Review Findings

Review date: 2026-06-27

## Context

The current staged approach stores note timing as exact beat fractions instead of decimal offsets or
ticks. Arrangement notes use `measure`, `beat`, optional fractional `offset`, and fractional
`durationBeats`. Tempo-map anchor seconds remain the only absolute seconds in the song document.

I reviewed the staged format rewrite plus the small unstaged validation/doc overlay. I did not edit
the staged changes during the review.

## Recommendation

The exact-fraction approach is better than the previous decimal/tick-offset approach. It keeps the
authored musical intent readable (`"1/3"`, `"3/16"`, `"1/8"`), supports tuplets exactly, and avoids
pretending note offsets have millisecond or sample precision. Keeping absolute seconds only on tempo
anchors is also the cleaner boundary.

After the findings below are addressed, the Jinjer project should be restructured to this new format.

## Findings

### P2: Save-time validation should reject invalid in-memory fractions

File: `rock-hero-common/core/include/rock_hero/common/core/fraction.h`

`Fraction` exposes mutable `numerator` and `denominator`, but package validation does not currently
reject `denominator <= 0`. A directly mutated in-memory `Fraction` can make the writer emit invalid
fraction text such as `"0/0"` or hit non-finite behavior in `toDouble()`, violating the writer rule
that save should not emit a document its own reader rejects.

The read path rejects bad persisted denominators, but the save path should validate the in-memory
value too.

### P3: Add a writer-output formatting assertion

File: `rock-hero-common/core/tests/test_rock_song_package.cpp`

The package tests round-trip notes, but they do not directly assert the written JSON shape. Since the
human-scannable one-line notes, anchors, and time-signature entries are part of the format goal, add a
direct writer-output test before treating the formatting as locked.

## Resolution

Resolved on 2026-06-27.

- Save-time note validation now rejects in-memory `Fraction` values whose denominator is not
  positive before doing fraction comparisons, duplicate-onset keys, or sustain-end math.
- Package writer coverage now directly asserts the one-line time-signature, anchor, and note row
  formatting.

## Verification Run

These checks passed during review:

```powershell
git diff --cached --check
git diff --check
powershell -NoProfile -ExecutionPolicy Bypass -File .\.codex\skills\rockhero-build\scripts\rockhero-build.ps1 -Targets rock_hero_common_core_tests,rock_hero_editor_core_tests -RunTouchedTests
```

Focused test results:

- `rock_hero_common_core_tests`: all tests passed.
- `rock_hero_editor_core_tests`: all tests passed.
- Touched-test run reported all selected test binaries passing.

## Next Steps

1. Restructure `C:\Users\tjwro\OneDrive\Desktop\Rock Hero Stuff\Jinjer - Pisces.rhp` to the exact
   fraction format now that the code review findings are resolved.
