# Start Here

*Snapshot taken 2026-09-18 at the end of a session, so the next one can open cold. The release-plan
section was amended 2026-09-19, and the status claims were refreshed 2026-09-24.*

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
**P7** the marker grammar and menu plane, **P8** the shift-slide look and hand-window ramp (the
look was redrawn after signing: arrival on the head `3e5fceae`, chip dropped `6fffd40f`, one
morph `db51a7fa` — the sighted fixes carry it). Already signed before that: P1, P2, P2b, P3, P9.

Signed 2026-09-24: **P5**, the harmonic verbs `H` and `Shift+H` with the node picker (withdrawn
2026-09-18 for more work; its keymap rows are no longer provisional), **#301 P10**'s three feel
questions, and **#270**'s batch remainder, #112's chip yield included.

The queue is empty apart from #298's end-to-end marker grammar pass (release plan, Phase 6).

Found at the 2026-09-24 sighting: a PALM-MUTED PINCH harmonic drew its pinch mark over the palm
mute in 3D (fixed the same day: the mute rungs sit at the top of the head-mark ladder); its 2D
readability is in `docs/tracking/backlog.md`.

## Open fixes carried

- **#271 — open strings ring far too long. FIXED 2026-09-19; re-sighted and CLOSED
  2026-09-24.** In Chop Suey an open G struck at 6:4 rang 49 beats. #186's lift was right and
  the PHRASE under it was not: bounded by bar-long silence alone, a song with no such rest was one
  phrase, so the open G rang toward a let-ring mark 36 bars later. A phrase is now a run of
  consecutive MARKED figures; the note stores 1 beat. The corpus moves with it (arpeggio spans
  1535 → 1399, since fewer runaway open rings fold into span onsets); census re-pinned 2026-09-20
  (`4a10e833`); the current drift is registered in `docs/tracking/watch-items.md`; the Chop Suey
  re-sighting signed it 2026-09-24.

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
played back. It is the ONE document that orders work; it names what already ships, the five decision
gates on the path, seven phases with exits, nine decisions (D1–D9), all ruled 2026-09-19, and
what is out of scope. The pull-off span defect and the two things it surfaced (the zero-length
span, the carried-ring founding rule) closed 2026-09-20, so **its next action is the ungated
builds**: the refusal flash (#278, `refusal-flash.md`), plan 60 Phase 0, plan 41 Phases 1–2,
tuning / capo. What actually ran from 2026-09-21 to 09-24 was the tick lattice, the derived shift
slide, the ring-ends plan's phases 1–2 and per-leg vibrato; the ungated list has not started.

FHPs / span markers are in scope through plan 60's hand marker, as are New Chart,
tempo/time-signature authoring, tuning/capo/cent-offsets, bend authoring, bulk editing and harmonic
display follow-ups **#2**, **#4** and **#9**. Follow-up **#3** is parked with artificial harmonics;
it becomes release scope only if they are re-enabled before release.

This folder was sorted on 2026-09-19. Eleven documents whose work was verified shipped moved to
`docs/plans/completed/`. Everything left here besides the release plan is a companion record —
the release plan's closing table says what each one is and which phase it feeds; three records
added since (derived shift slide, ring ends, keyframe ground-up) are now in that table.

## Not covered here

Recent commits touch the signal chain, including the per-tone level control and a planned Gain
block with a meter-only panel. That is a separate live thread and this note says nothing about its
state.
