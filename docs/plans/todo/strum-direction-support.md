# Strum-Direction Support — a distant pedagogy feature, deliberately not a chart field

Status: **DELIBERATELY DEFERRED 2026-08-27** — recorded at the user's direction during the
chart-ruleset walkthrough (`docs/plans/in-progress/chart-ruleset.md` [D7]). This is a
may-happen-someday plan per the `docs/plans/todo/` charter: treat everything below as possibly
stale until the day it is picked up, and re-verify against the current code first.

## The ruling this rides on

D7 ruled strum direction OUT of the chart record: it changes neither the pitches nor the
posture, note detection cannot verify it from audio (the game could never score it), and every
ruling of the 2026-08-27 walkthrough moved the record toward "what sounds and what the hand
verifiably does." At import, GP's direction marks (beat-level `Brush`/`Direction`, `PickStroke`)
drop with a count (backlog entry of the same date).

## Why it may return anyway — the user's rationale, verbatim in substance

Strum direction "is useful for teaching/learning and may be useful in charts specifically made
for learning purposes, or to notate PRECISELY how the guitarist played a passage in
exceptionally difficult charts to aid in learning the song." Both uses are real: strum patterns
are how rhythm guitar is taught, and a faithful down/up transcription of a hard passage is
genuine pedagogy data a chart-as-teaching-tool could carry.

## The shape it must take, decided now so the deferral survives

- **A pedagogy-surface datum, never a chart-truth field.** The D7 ruling stands: the truth
  record stays direction-free. If a teaching surface (the fingering-over-time display is the
  standing candidate) wants direction, the datum enters at that layer, alongside fingering —
  the same layer published notation puts it on.
- **Import can then revive the dropped marks**: the count added under D7 tells us how much the
  corpus carries the day this is picked up.
- **Displays**: down/up marks are standard notation vocabulary (SMuFL carries the glyph family);
  both surfaces would need a mark ruling via ui-design-expert before anything draws.

## Trigger

Pick this up only when a teaching/learning chart mode or the pedagogy display is being designed
— not before. It has no dependency on, and grants none to, the derivation package.
