# Plan 69 — Highway Display Toggles

**Status:** Drafted 2026-10-11 against `master @ 9aec2de0`. **Phases 1 and 2 landed 2026-10-11**
(View > Highway Display; shared `highway.show*` keys); the Phase 2 sighting is pending. The user wants three
reading aids to be player choices rather than fixed behavior. Where the choices are stored is open
(69-Q1) is answered by plan 70's shared settings file; the toggles themselves are decided.

## Goal

Three elements of the 3D highway become player toggles, because some players will want them and
others will not:

| Toggle | What it draws today | Default |
|---|---|---|
| **Anticipation ring** | A hollow copy of the head parked at the hit line on the note's landing slot, growing to full size as the note arrives (`highway_renderer.cpp`, the "Anticipation ring" block) | decided by 54-Q21's sighting |
| **Note rotation** | The rolling flip: single notes stand vertical as they enter the window and roll flat around their travel axis, landing flat before the hit line (the "Rolling flip" block); chords never roll | on |
| **Arpeggio brackets** | The 3D bracket pair at an arpeggio span's start (atlas cells 8 and 9, `push_bracket`) | on |

Each is a viewing preference, not notation the chart lacks, so turning one off never changes what
the chart says, only what this player chooses to see. Defaults reproduce today's look apart from
the ring, whose default 54-Q21 now decides in place of keep-or-drop.

## Non-goals

- **The 2D tab lane.** It is the authoring view and always draws its arpeggio brackets; the ring and
  the rolling flip do not exist there. "Surfaces must not diverge" is about what notation each
  surface can show, and a 3D reading preference does not take that capability away.
- **Theme keys.** Toggles are player settings beside the theme (plan 54, "What a theme captures"),
  never part of a theme file.
- **Pickers for anything else.** Theme, palette, head shape and inlay pickers are plan 54 Phases 2b
  and 5.

## Design

- **`HighwayViewPreferences`** (plan 70) holds the player's shared reading choices, `mirrored`
  and `invert_string_order` today, and gains the three toggles as `bool` fields, positively named
  so `true` draws the element. It is nested in the renderer's `HighwayDisplayOptions` (which
  reaches the renderer through `setDisplayOptions`) and in the shared `HighwayPlayerSettings`, so
  the field list is stated once. Final field names are chosen at implementation, checked against
  the naming conventions. If Phase 1 lands before plan 70 Phase 1, the three fields go into
  `HighwayDisplayOptions` and move with `mirrored` and `invert_string_order` when plan 70 nests
  them.
- **The renderer** reads each at its one draw site and skips the element. Rotation off means a
  single note travels flat for its whole approach, exactly as it lands today; nothing else about
  the head's placement changes.
- **Arpeggio brackets off** hides the bracket cells. Whether the bracket's printed posture and stop
  hide with them is 69-Q2. The arpeggio rails still mark the span either way. It also hides the
  arpeggio-styled panel the cells sit on: without its brackets that panel reads as a chord box,
  which marks simultaneity (shape rule 5), drawn around what may be one note of a broken chord. A
  strum the mark stood in for wears its own chord box again.
- **Both products** expose the three toggles: the editor's View menu and the game's options
  (plan 26's settings surface, 54-Q16's precedent). The editor preview follows the editor's
  setting.

**CI note.** A struct gaining fields breaks every designated initializer of it anywhere in the tree
(`-Wmissing-designated-field-initializers`); sweep by a sibling field name (`rg '\.mirrored = '`),
never by the type name.

## Open questions

- **69-Q1 — where the toggles are stored. Answered 2026-10-11 by plan 70:** in the `highway`
  keys of the shared settings file, as fields of `HighwayViewPreferences`, so both apps read one
  value. Phase 2 builds on them once plan 70's Phase 2 lands.
- **69-Q2 — what "arpeggio brackets off" hides.** (A) Only the bracket cells; the posture and stop
  printed beside them stay. (B) The bracket and everything it prints. *Recommended: B*, since the
  printed stop reads as part of the bracket and would float unanchored without it; the rails keep
  the span visible. *Found at implementation:* the 3D board prints no posture digit or satellite
  (only the 2D lane reads `ShapeStringViewState::digit`); a 3D bracket states its stop by where it
  sits. So on the board A and B hide the same thing, and the question only bites if 3D ever gains
  the digit.
- **69-Q3 — the ring's default.** Taken from 54-Q21's side-by-side sighting.

## Phases

1. **The options and the draw sites.** Three fields, three skips, defaults as above (the ring's
   from 54-Q21). Tests: the renderer smoke test brings the renderer up with each toggle off; any
   pure helper a skip touches keeps its unit tests. **Verification:** build, touched tests.
2. **Persistence and menus**, on plan 70's shared `highway.*` keys: the three fields in
   `HighwayViewPreferences` with their round-trip test, the editor View menu entries, and the
   game's options entries. **Verification:** build, touched tests. **STOP: sighting in both products,
   each toggle flipped in motion.**

## Dependencies

- Plan 54-Q21: the ring's default. Phase 1 can land with the ring's current behavior as the
  default and take the ruling as a one-value change.
- Plan 26 for the game's options surface; until it exists the game reads the stored values only.
- Plan 70 Phase 2 (the shared `highway.*` keys) for Phase 2.
