# Start Here

*Snapshot taken 2026-09-18 at the end of a session, so the next one can open cold.*

**The session task list is authoritative; this file is a convenience copy.** Where the two
disagree, believe the task list and fix this file. Re-verify anything here against the code before
acting on it — every claim below was true on the date above and nothing keeps it true.

## Do this first

**Settle the P10 ruling (#301).** It is a decision, not code, and it gates the work behind it.

The caret can now walk onto a SECTION row above the top string and a TONE row below string 1.
Standing between markers on such a row, the governing marker is already selected, so the marker
chord takes rule 1 of the grammar and RESTATES that marker instead of inserting where the caret
stands. On the section row that means `Ctrl+M` can never add a section while you are on the row.

The proposal on the table: **on a marker's OWN row the chord goes POSITIONAL** — restate a marker
at the caret, insert where there is none. It differs from today in exactly one cell, standing
between markers, because standing ON a marker already selects it. For the tone row it yields
boundary restates and mid-region splits, which is what `Ctrl+T` already does off the row.

The consequence to accept or reject: `Enter` and the chord would then diverge on the row, `Enter`
restating what you are IN and the chord acting WHERE you are.

Full statement of the law, and why it is shaped this way, is in
[marker-verb-grammar.md](marker-verb-grammar.md).

## Sighting queue

Signed 2026-09-18: **P4** sections from the keyboard, **P6** the typed-note entry grammar,
**P7** the marker grammar and menu plane, **P8** the shift-slide look and hand-window ramp.
Already signed before that: P1, P2, P2b, P3, P9.

Withdrawn: **P5**, the harmonic verbs `H` and `Shift+H` with the node picker, went back into ACTIVE
WORK before being sighted. Its keymap rows stay PROVISIONAL and it needs a fresh sighting item once
that work settles.

Still open:

| Item | What |
|---|---|
| **#301 P10** | The caret's section and tone rows. Carries the ruling above plus three things to watch: whether the pinned chip shows its SELECTED state as you walk, how the sparse section row feels at a fine grid, and whether the tone row retoning what you hear at each boundary reads as helpful or as noise. |
| **#270** | The 2026-09-02 batch remainder: the same-fret settle, the pinned tone chips, and #112's chip-yield ruling. The legend display form is already ruled and signed. |

## Open fixes carried

- **#271 — open strings ring far too long.** Sighted 2026-09-18 and judged NOT right: in Chop Suey
  an open G struck at 6:4 rings 49 beats, about 12 bars. The cause is the let-ring phrase cap, not
  texture, and it follows from the law signed as **#186**, which makes open strings immune to
  clipping until the whole let-ring phrase ends. Fixing it therefore AMENDS A SIGNED LAW rather
  than repairing code that failed its spec. Size it after reading how the cap is expressed.
- **The section insert resolves its position when the prompt is ACCEPTED, not when the key is
  pressed**, so a section added while the transport rolls lands where the playhead drifted to.
  Written up with the fix spelled out at the end of `docs/tracking/backlog.md`. The tone marker does
  NOT have this bug; it captures at press time and carries the position through its picker callback.

## Awaiting review

`marker-verb-grammar.md` in this folder is the design record for the marker work, written to be
handed to a higher-level review. Goals first, then what each commit did, then the design, then the
checklist a NEW marker kind follows, then the open questions with their counter-cases. The two most
likely to draw fire are the catalog pruning a tone that loses its last reference on retone, and
whether minting belongs inside the retone at all.

## The larger queue behind all this

`#252` **G0** is the umbrella: minimum editing functionality, meaning author a full tab except FHPs
and span markers. Under it, unstarted: `#259` **G7** tuning, capo and cent-offset dialog; `#260`
**G8** copy, paste, transpose and select-all; `#261`–`#264` **G9** the bend display anchor, the
keyframe ruling bundle, W9-F, and bend authoring itself; `#265` **G10** tempo map and time-signature
authoring, which gates the New-chart entry point.

## Not covered here

Recent commits touch the signal chain, including the per-tone level control and a planned Gain
block with a meter-only panel. That is a separate live thread and this note says nothing about its
state.
