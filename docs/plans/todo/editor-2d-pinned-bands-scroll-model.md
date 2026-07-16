# Editor 2D View — Pin Primary Bands, Scroll Only Automation Lanes

**Status:** deferred idea, not started. Surfaced 2026-07-15 while adding the 2D song-section lane;
the section lane shipped in the ruler (pinned under any scroll model), so it does **not** depend on
this. Re-verify the code against this doc before executing.

## Context / problem

Today the timeline ruler (`TimelineRuler`) is the only vertically pinned band. Everything below it —
the waveform/tab row (`ArrangementView` + overlaid `TabView`), the tone strip (`ToneTrackView`), and
the automation lanes (`ToneAutomationLanesView`) — lives in one `juce::Viewport` content canvas
(`TrackViewport::m_content`) and scrolls together vertically. So on a tone with many automation lanes,
scrolling down to a lane pushes the note highway and tone strip off-screen.

The primary editing content (the notes and the tone strip) arguably should stay visible while only the
secondary, expandable automation lanes scroll.

## Proposed change

Pin the waveform/tab row and the tone strip along with the ruler, and give **only** the automation
lanes their own inner vertical scroll region. Layout becomes: pinned {ruler, waveform+tab, tone strip}
on top, a scrolling lanes viewport below.

### The one real complication — lanes scrollbar on tone switch

The lane set shown is per active tone, and tone lane counts vary widely. If the user is scrolled deep
into a 10-lane tone and switches to a 1-lane tone, a naive shared offset would leave the scrollbar far
down, hiding the single lane. Clean fix: reset the lanes inner viewport to the top on every
displayed-tone change (`juce::Viewport::setViewPosition({0, 0})`). JUCE already clamps a viewport's
view position when its content shrinks, so the user can never be stranded past the end; the reset just
makes the common case land on the first lane.

## Open design question (must resolve before building)

The waveform+tab row height is **not** fixed — `TrackViewport::primaryTrackHeight()` scales it by string
count, and past the six-string reference the row grows and today relies on the shared vertical scrollbar
to absorb the overflow (7+ string tabs, wide tunings). If that row becomes pinned, a very tall tab has
no shared scrollbar to fall back on. Decide how a pinned tab taller than its slot behaves: cap the
pinned tab height and give the tab its own inner scroll, let the pinned region grow and shrink the lanes
area, or keep the tab in the scrolling region and pin only ruler+tone. This is the crux that makes this
a design task rather than a mechanical change.

## Touch points (current code)

- `rock-hero-editor/ui/src/timeline/track_viewport.{h,cpp}` — `resized()` / `layoutScaledCanvas()` split
  the pinned bands from the scrolling lanes; today `m_viewport` holds everything below the ruler.
- `rock-hero-editor/ui/src/timeline/tone_automation_lanes_view.{h,cpp}` — the lanes view would live in
  the new inner scroll region; wire the reset-to-top on displayed-tone change.
- `rock-hero-editor/ui/src/timeline/cursor_overlay.*` — the full-canvas overlay (playhead, snap guide,
  section boundary lines) must still span the visible content correctly across the new split.
