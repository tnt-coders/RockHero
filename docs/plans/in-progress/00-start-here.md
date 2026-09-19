# Start Here

*Snapshot taken 2026-09-18 at the end of a session, so the next one can open cold. The release-plan
section was amended 2026-09-19.*

**The session task list is authoritative; this file is a convenience copy.** Where the two
disagree, believe the task list and fix this file. Re-verify anything here against the code before
acting on it — every claim below was true on the date above and nothing keeps it true.

## Do this first

**P10 (#301) is settled.** On a marker's own row, the chord stays positional: it restates a marker
exactly at the cursor and inserts or splits where there is none. The selected holder chip is row
focus only, not the authoring target. `Enter` still restates the selection, so `Enter` and the
marker chord intentionally diverge on the row.

Regression coverage lives in `test_editor_controller_marker_rows.cpp`: the section row inserts at
a free cursor measure even while the earlier section is selected, and the tone row splits at the
cursor inside the selected region.

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
| **#301 P10** | Settled: row chords stay positional. Still sight the three feel questions: whether the pinned chip shows its SELECTED state as you walk, how the sparse section row feels at a fine grid, and whether the tone row retoning what you hear at each boundary reads as helpful or as noise. |
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

`#252` **G0** is now the first releasable editor bar, not the old narrow minimum-editing bar. The
working plan is [first-releasable-editor.md](first-releasable-editor.md): a new project can be
created from scratch, and every supported chart fact can be authored, edited, saved, reopened and
played back. It is the ONE document that orders work; it names what already ships, the six decision
gates on the path, seven phases with exits, nine open decisions (D1–D9) waiting on the user, and
what is out of scope. **Its next action is the user's pass over D1–D9**, then the ungated builds:
the non-modal notice channel, plan 60 Phase 0, plan 41 Phases 1–2, tuning / capo.

FHPs / span markers are in scope through plan 60's hand marker, as are New Chart,
tempo/time-signature authoring, tuning/capo/cent-offsets, bend authoring, bulk editing and harmonic
display follow-ups **#2**, **#4** and **#9**. Follow-up **#3** is parked with artificial harmonics;
it becomes release scope only if they are re-enabled before release.

This folder was sorted on 2026-09-19. Eleven documents whose work was verified shipped moved to
`docs/plans/completed/`. Everything left here besides the release plan is a companion record —
the release plan's closing table says what each one is and which phase it feeds.

## Not covered here

Recent commits touch the signal chain, including the per-tone level control and a planned Gain
block with a meter-only panel. That is a separate live thread and this note says nothing about its
state.
