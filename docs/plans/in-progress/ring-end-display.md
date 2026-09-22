# Where a ring's end statement is drawn

Status: OPEN for discussion, 2026-09-22. Written so the question survives a session restart. The
model it sits on is signed (`derived-shift-slide.md`, and the ring's-end work recorded in
`keyframe-and-burst-ground-up.md`): the STORED chart holds the truth — a slide-out's fret, an end
bend or a shift slide's arrival stands exactly at the ring's end, which may be exactly on the
next head of the same string — and PRESENTATION spaces it. What is open is only HOW each surface
spaces it.

## What is built (HEAD 36b6cbee)

- The trim (`chart_presentation.cpp`) shortens the presented ring by one minimum sustain distance
  (100 ms) before the binding onset and CARRIES the end's statement to the presented end. Both
  surfaces and scoring read the presented note, so on both the end statement is drawn 100 ms
  early, and the hand window completes with the rail at that drawn instant.
- `Alt`, a selected note and the caret peek reveal the stored form: the end statement draws on
  the head.

## The sighting, 2026-09-22

- **2D reads right.** The lane is dense; a slide-out chip or arrival head drawn 100 ms before the
  next head keeps the two apart, and the retreat is what makes them readable.
- **3D reads wrong.** Moving the keyframe 100 ms earlier on the highway does not sight well: the
  rail and the hand window complete a margin short of where the sound goes. The user's reading:
  in 3D the tail should simply be CROPPED 100 ms before the next note, and the keyframe should
  not move.
- **Before the hand-window fix**, the window completed at the head on the smooth (pitched) ease
  and sighted well — almost better than the fretted rail, which completed early. That is the
  picture the user liked.

## The question

Should the retreat be a 2D fact only, with the highway drawing the end statement where it is
stored and the tail cropped short of the head — or should the retreat stay one presentation
rule for both surfaces, with something else giving 3D the smooth completion at the head?

## Options to weigh

1. **Per-surface spacing.** Presentation keeps the stored end; the 2D lane retreats the end
   statement by the margin when it draws; the highway draws the statement where it is stored and
   crops the tail's ink at the margin. Cost: the surfaces-must-not-diverge rule
   (`feedback_surfaces_must_not_diverge`) says neither surface gets a notation the other cannot
   show — but this is spacing, not notation; both show the same statement. Also "what is
   displayed is what is scored" then means scoring reads the stored instant, which is the truth.
2. **Arrivals are not endings.** Keep one presentation rule, but the retreat spaces ENDINGS only:
   a slide-out, an end bend or a bare tail ends and keeps the margin; a shift slide hands its
   sound to the next note and its ring runs to the head on both surfaces. This gives 3D the
   completion-at-the-head the user liked for shift slides, and 2D the diagonal running into the
   next head (which the user expected under `Alt`). Slide-outs still complete a margin early in
   3D, which the sighting may or may not accept.
3. **Both.** Option 2's rule for arrivals, and option 1's cropping for endings in 3D.

## Related, decided the same day (see the commit log)

- One smooth ease for every glide, hand and finger alike; slide-out-ness affects only the rail's
  dimming, never motion (the "arrives with slope" ease and its corner go). Discussed, not built.
- 2D draws every end statement as the chip, arrival or slide-out; the next head says which it
  was. Discussed, not built.
- Only `Alt` reveals the stored form; selection and the caret peek do not (built with the
  arrow-stop fix, so a clicked end chip does not jump).

## What to sight to decide

My Sacrifice measure 9 (the chord shift slide with open strings ringing through), a plain
slide-out abutting a same-string head, and an end bend abutting one — each in 2D, in 3D, and with
`Alt` held.
