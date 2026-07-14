# Plan 51 — Time-space camera and smooth-scroll playback follow

Status: **Parked 2026-07-14** — the live evaluation reversed the adoption (see Decision
record); the shifted window stands and the spike was removed. Execute only if a driver for
smooth scrolling in the *editor* returns; re-verify the inventory first. | authored 2026-07-13
| baseline `work-in-progress @ 7e0a52ee`

## Goal

Make fixed-cursor smooth scrolling the editor timeline's playback follow, implemented honestly:

1. **Time-space camera.** Replace the full-song-width content canvas + integer viewport scrolling
   with a camera (`left edge time` + `pixels per second`) that paints only the visible window
   with subpixel time→x mapping, so scrolling glides instead of ticking.
2. **Clock-driven follow.** During playback the cursor pins at a fixed fraction of the view and
   the content flows past it, driven by the shared playback clock + extrapolator (plan 12), so
   scroll velocity is smooth instead of stepping with audio blocks.
3. **No regressions while stopped.** Manual scrolling, wheel zoom, click-to-seek, drag edits,
   loop/ruler interaction, cursor recenter on load, and zoom persistence all keep their current
   behavior; the camera only changes who owns the horizontal axis.

## Decision record

- 2026-07-03: both follow modes spiked over a bare waveform; shifted window adopted, smooth
  scroll deferred until tablature rendering made the sight-reading comparison honest
  (docs/todo/smooth-scroll-follow-evaluation.md, now superseded by this plan).
- 2026-07-13: spike re-landed behind View → Smooth Scroll Follow (`55f49476`); the user ran the
  deciding sight-reading test with real tablature and adopted smooth scrolling ("I'm sold"),
  tuning the pin fraction 1/3 → 0.05 → 0.2 → 0.15 → 0.1. The known spike artifacts —
  integer-pixel ticking and block-quantized velocity shimmer — were called out and accepted as
  exactly what this plan removes.
- 2026-07-14: **adoption reversed after further live use** — smooth-scrolling tablature proved
  harder to sight-read, exactly the reading-vs-timing tradeoff the original evaluation
  predicted (fixations need stationary symbols; part of the difficulty was spike jitter the
  camera would fix, but not the physiology). The desire that had been driving the fixed-cursor
  preference turned out to be a separate feature: a **cursor-locked chord/arpeggio posture
  display** (span identity + held frets traveling with the playhead for the span's duration —
  note heads never move; a held note's presence at the cursor is its sustain tail, RS-style).
  That feature is follow-mode-independent, designed but deliberately **not built** (user chose
  to return to a clean baseline first); the design is recorded in this entry for whenever it is
  picked up. The spike (PlaybackFollowStyle, followCursorSmoothly, the View toggle) was deleted
  and the shifted window stands as the editor's follow. Scrolling-tab play-along remains the
  game's territory (docs/roadmap/30-game-2d-tab-view.md).

## Non-goals

- Changing vertical scrolling (lane overflow keeps the existing pixel-scrolling viewport axis).
- Migrating every `ITransport::position()` read in the editor to `IPlaybackClock` (plan 12's
  12-Q2 stays narrow: this plan migrates only the per-frame follow/cursor sampling path).
- Any game-side change: the game highway already runs on the clock + extrapolator.
- Scroll-speed/pin-fraction user settings UI (constant for now; a future settings surface can
  expose it).
- Perceptual smoothing beyond the extrapolator (no motion blur, no sub-frame interpolation of
  paints).

## Constraints

Applicable subset of the roadmap's non-negotiable constraints plus design-doc anchors:

- (a) **Layering**: camera state and follow math are pure editor-core logic
  (docs/design/architectural-principles.md "Time Must Be a Dependency" — the camera consumes
  injected time samples, never reads clocks); `rock-hero-editor/ui` keeps only painting, JUCE
  scrollbar/gesture wiring, and the vblank pump.
- (b) **Ports**: playback time comes from the existing `common::audio::IPlaybackClock` snapshot +
  `PlaybackClockExtrapolator` (plan 12, shipped surfaces — verified at
  `rock-hero-common/audio/include/rock_hero/common/audio/clock/`); no new audio-side surface.
- (h) **Builds** through `.agents/rockhero-build.ps1`; batch verification per phase.
- No hybrid horizontal regimes: the camera owns horizontal placement in **all** states (playing,
  paused, manual scroll). The 2026-07-03 analysis flagged a Viewport-while-stopped/camera-while-
  playing split as a transition-bug factory; this plan keeps one regime.

## Current-state inventory (verified 2026-07-13 @ 7e0a52ee)

- `rock-hero-editor/ui/src/timeline/track_viewport.{h,cpp}` — the refactor's center:
  - `Content` canvas sized `scaledContentWidth()` = timeline duration × `m_pixels_per_second`
    (up to 1264 px/s), hosted in a `juce::Viewport` (`TimelineViewport`) that owns both axes in
    integer pixels; `setViewportLeft(int)` clamps and moves.
  - Children spanning the full canvas width: `ArrangementView` (waveform), `TabView` (tablature
    overlay), `ToneTrackView`, `ToneAutomationLanesView`, `CursorOverlay` (all mapped by content
    width), plus the pinned `TimelineRuler` fed by `updateRulerView()` with
    `(window, content_width, view_x)`.
  - `juce::VBlankAttachment` already pumps `updatePlaybackFollow()` + `updateRulerCursor()`.
  - Follow modes: `followCursorWithWindowShifts` (shifted window, default) and
    `followCursorSmoothly` (spike, `g_smooth_follow_pin_fraction{0.1}`), toggled by
    `PlaybackFollowStyle` via View → Smooth Scroll Follow (id 204 in
    `rock-hero-editor/ui/src/main_window/editor_view.cpp`, unpersisted).
  - Grid economy: `refreshTimelineGridForViewChange()` skips the tempo-map scan when the
    horizontal span is unchanged — a check that stops helping while scrolling every frame; the
    scan itself is span-bounded over derived index tables (the plan-4x grid-perf work).
  - Follow reads `m_transport.position()` (message-thread `ITransport`), block-quantized — the
    velocity-shimmer source.
- `common::audio::IPlaybackClock::snapshot()` (implemented by `Engine`) +
  `PlaybackClockExtrapolator` (monotonic, pause-hold, snap-on-seek — tests in
  `rock-hero-common/audio/tests/test_playback_clock_extrapolator.cpp`); the editor 3D preview
  (plan 44) already runs clock-driven follow on these.
- `TabView` is already camera-shaped: `setVisibleTimeline(range)` maps its bounds to the range
  (its tests drive it that way), so a moving window is a data change, not a rework.
- Geometry-coupled tests: `rock-hero-editor/ui/tests/test_editor_view_timeline.cpp` constructs
  `(content_width, view_x)` ruler views and asserts viewport/canvas bounds and view positions;
  these rewrite to camera terms.

## Phased implementation

### Phase 1 — Headless timeline camera (editor/core)

A pure `TimelineCamera` value type + functions in `rock-hero-editor/core` `timeline/`:
`{ left_edge_seconds, pixels_per_second }` with time↔x mapping for a given view width, edge
clamping against the timeline range, zoom-about-anchor (preserving the anchor's screen x),
follow placement (`left = time − view_seconds × pin`), and a follow-engagement state machine
(playing + not user-suspended; manual horizontal gesture suspends, seek or playback restart
re-engages — the arbitration rule from the superseded evaluation doc). Unit tests cover
mapping round-trips, clamping at both edges, zoom anchoring, and the suspend/resume transitions.

Exit: camera math and arbitration fully tested with no JUCE dependency.

### Phase 2 — Camera-owned horizontal axis in TrackViewport

Rework `TrackViewport` so the canvas is **view-sized** and painted from the camera in every
state:

- The `juce::Viewport` keeps only the vertical axis (lane overflow); horizontal scrollbar
  becomes a project-owned `juce::ScrollBar` mirroring the camera (range = timeline, thumb =
  visible window), with drag driving the camera.
- Children resize to the view and receive the camera's visible window each change (`TabView`
  unchanged in kind; `ArrangementView`, `ToneTrackView`, `ToneAutomationLanesView`,
  `CursorOverlay`, the grid canvas, and `TimelineRuler` converted from content-width mapping to
  visible-window mapping — one shared mapping seam, no per-view formulas).
- All existing behaviors re-expressed as camera operations and verified: wheel zoom about the
  pointer, click-to-seek, stop-button reset to zero, `requestCursorFocus()` recenter,
  restored-zoom clamping, tone-row edge drags and automation interactions (hit tests now map
  through the visible window), plan-47 forward-compatibility (ruler stays pinned; the future
  loop-drag band is camera-agnostic).
- Grid/ruler feeds: per-change span computation from the camera; the unchanged-span skip stays
  for vertical scrolls and no-op updates.

Tests: `test_editor_view_timeline.cpp` rewritten to camera terms; per-view tests unchanged
(already range-driven). Exit: identical interactive behavior while stopped, subpixel-capable
horizontal placement, no full-width canvas anywhere.

### Phase 3 — Clock-driven smooth follow

Per vblank while following: read `IPlaybackClock::snapshot()`, advance the
`PlaybackClockExtrapolator`, and hand the extrapolated time to the camera's follow placement;
`CursorOverlay` draws the playback cursor from the **same** per-frame time sample so cursor and
content cannot diverge (the cursor sits visually pinned at the fraction). Paused/stopped keeps
the current sampling path. Delete the spike: `PlaybackFollowStyle`, the View menu toggle, and
`followCursorWithWindowShifts` + its tuning constants go per 51-Q1.

Exit: playback scroll is visually smooth (no per-pixel ticking, no velocity shimmer) at typical
zooms; pause/resume/seek/loop behave per the extrapolator's tested semantics.

### Phase 4 — Frame-budget verification

Measure per-frame paint cost during playback at representative zooms (waveform `drawChannels`,
tab lane, grid dots, ruler) on the dev machine. Add the ruler label text-width memo cache only
if measurement shows label re-measurement hot; likewise any other cache earns itself by
measurement, not speculation. Exit: sustained follow at the display refresh rate without paint
backlog; findings recorded here.

### Phase 5 — Final acceptance

Full build + all test suites via the helper (separate invocations); update
docs/todo/smooth-scroll-follow-evaluation.md (already a pointer) and the roadmap status board;
witnessed user pass: sight-read a real chart during playback, manual scroll while playing
(suspend/resume), zoom during playback, loop wrap once plan 47 lands.

## Open questions

- **51-Q1** shifted-window fate: (a) delete the shifted-window follow and the toggle entirely —
  smooth scroll is the one follow mode (no-legacy rule; dead modes rot); (b) keep both behind
  the View toggle as a user preference. **R: a** — the deciding evaluation is done; a
  preference can be re-added later from git history if ever wanted.
- **51-Q2** follow suspension gestures (moot if 51-Q3 = b): (a) suspend only on explicit
  horizontal navigation (scrollbar drag, horizontal wheel scroll, middle-drag pan if added);
  re-engage on seek or play toggle; (b) also suspend on any canvas mouse-down (edit gestures
  pause the scroll). **R: a** — editing mid-playback over scrolling content is its own UX
  question; do not couple it to navigation arbitration until it is observed to be a problem.
- **51-Q3** cursor lock scope (user-raised 2026-07-13: "make the cursor legitimately fixed and
  have scroll move the chart around the cursor"): (a) cursor locked to the pin **during
  playback only**; while paused the view scrolls freely, the cursor stays put, and
  click-to-seek moves only the cursor line; (b) cursor locked **always** — the view is a pure
  function of cursor time, and every scroll or seek moves the content around the pinned
  cursor. **R: a.** Honest simplification accounting for (b): it removes one state variable
  (the independent left edge), the 51-Q2 arbitration machinery, all off-screen-cursor cases,
  and the recenter paths — but none of Phase 2, which is identical either way. Its costs: the
  settled editing grammar makes every empty-canvas click a seek+deselect, so under (b) every
  such click jumps the whole canvas to re-pin the clicked point under the cursor; and
  glancing ahead/behind without moving the playhead becomes impossible (paused browsing drags
  the cursor; during playback it is an audible seek). No tab editor or DAW locks the view
  while paused; rhythm games do but have no editing surface. Under the camera both options
  are one placement-policy function, so (b) remains a cheap later switch if editing practice
  proves the jumps harmless.

## Final acceptance bundle

- Build + all seven suites green through `.agents/rockhero-build.ps1` (separate invocations).
- clang-tidy pass user-triggered (fix findings when triggered).
- Witnessed checklist from Phase 5 signed off by the user.

## Forward notes

- Plan 12's 12-Q2 (editor cursor migration to the clock) is partially executed here (the
  per-frame follow/cursor path); a later cleanup may migrate the remaining editor
  `ITransport::position()` reads and retire the duplicate sampling, but nothing forces it.
- The camera seam is what the game's 2D tab strip (plan 30) and any future editor minimap want
  to share conceptually; keep the mapping helper's shape reusable but do not promote it to
  common until a second consumer exists.
