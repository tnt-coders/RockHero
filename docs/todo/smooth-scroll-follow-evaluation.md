# Smooth-Scroll (Fixed-Cursor) Playback Follow — Deferred Evaluation

**Status:** Deferred until tablature notes render over the waveform in the editor timeline.
**Decision captured:** 2026-07-03.

## Decision this defers

The editor's playback follow is the Guitar Pro-style **shifted window** (adopted 2026-07-03,
implemented in `EditorView::TrackViewport::followCursorWithWindowShifts` in
`rock-hero-editor/ui/src/editor_view.cpp`): the cursor travels across a stationary window; when
it reaches the trigger fraction of the view width (0.8) the window glides forward over 0.3s with
a cubic ease-out so the cursor resumes from the pin fraction (0.05); a cursor off-screen left
snaps straight to the pin; a cursor at or past the trigger — including beyond the right edge —
glides.

The alternative is **fixed-cursor smooth scrolling**: the cursor pins at a fixed fraction of the
view and the content scrolls continuously, like a rhythm-game note highway. A spike of this mode
was built and evaluated on 2026-07-03 (a per-vblank `setViewportLeft` pinning the cursor a third
of the view width from the left) and the user judged it better than expected and immersive — but
the shifted window was adopted for now. Recover the spike from git history (the
`PlaybackFollowStyle` enum and `followCursorSmoothly`, removed the same day) if a quick re-spike
is useful.

## Why the evaluation is deferred, and what re-triggers it

The spike comparison was made over a scrolling **waveform** — texture, with nothing to read.
That validates smooth scrolling for *feel* but cannot test its real cost: sight-reading moving
tablature. The two modes optimize different cognitive tasks:

- **Shifted window** keeps content stationary while the eyes read ahead — best for decoding
  pitch/fingering content, which is why tablature editors (Guitar Pro et al.) use it.
- **Smooth scroll** encodes timing spatially (constant velocity means distance-to-cursor equals
  time-to-play) — best for play-along timing, which is why rhythm games use it, and it matches
  the future game side's fixed-hit-line highway.

**Re-run the comparison when the editor renders tablature notes over the waveform**, at the zoom
level real tabs will use. That is the earliest point the reading-versus-timing comparison is
honest.

## What adopting smooth scrolling would commit to

Findings from the 2026-07-03 analysis (verify against current code when picked up):

1. **Time-space camera.** `juce::Viewport` scrolls in integer pixels; at low zoom the smooth
   scroll visibly ticks instead of gliding. The honest implementation replaces the
   full-song-width content canvas + pixel `view_x` with a camera of `left_edge_time` +
   `pixels_per_second`, painting only the visible window with subpixel time→x mapping. This also
   removes the huge zoomed canvas (duration × up to 1264 px/s wide component) — a latent smell —
   but re-owns scrollbars, drag-scroll, and hit-testing, and touches every test that constructs
   `content_width`/`view_x` geometry. Avoid a hybrid (Viewport while stopped, camera while
   playing): two rendering regimes breed transition bugs.
2. **Transport extrapolation.** `Engine::position()` reads Tracktion's
   `EditPlaybackContext::getAudibleTimelineTime()`, an atomic written once per audio block by
   `PlayHeadPositionNode` (latency-compensated but block-quantized, ~5–11 ms steps). Invisible
   for a thin cursor; visible as velocity shimmer when the whole content field moves. UI-side
   extrapolation (last audible time + wall-clock delta, gently corrected) fixes it.
3. **Per-frame render budget.** Continuous scrolling runs every view-change-gated path at frame
   rate: the visible-span tempo-grid rescan (already cheap and incremental), ruler label
   re-measurement (needs a text-width memo cache), waveform `drawChannels`, and a full-viewport
   repaint under JUCE's software renderer.
4. **Gesture arbitration.** Manual scrolling and on-canvas drags must suspend the follow
   (resume on seek/play) or the per-frame repin fights the user. The shifted window needs a
   milder version of the same rule eventually — today an off-screen-left cursor snaps back even
   if the user is deliberately browsing ahead during playback.

If the shifted window wins permanently, none of the above is ever needed — it runs on integer
viewport positions with occasional short animations.

## Evaluation criteria for the re-run

- Sight-reading real tablature at play-along zoom in both modes (the deciding test).
- Immersion / play-along feel (smooth scroll won this on the waveform-only spike).
- Consistency with the game's note-highway presentation.
- Implementation cost actually remaining at that time (items 1–4 above).
