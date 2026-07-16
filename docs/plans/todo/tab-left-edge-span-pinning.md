# Tab left-edge span-identity pinning

*Status: deferred (designed 2026-07-15, not built). Per the `docs/plans/todo/` rules, re-verify
against the current code before executing — especially the chord-chip location, which moves from
the ruler into `TabView` in the ruler-redesign change set this plan was split from.*

## Goal

While a chord/arpeggio span, a sustained note, or an arpeggio posture crosses the **visible left
edge**, keep its identity readable at that edge instead of letting it scroll away:

1. **Chord/arpeggio name chips** — the active span's name chip holds at the left edge and slides
   off only as the span itself ends (exactly the tone-region label rule in
   `ToneTrackView::paint`, rock-hero-editor/ui/src/tone/tone_track_view.cpp).
2. **Sustained notes** — a note whose head is off-screen left but whose tail still crosses the
   edge redraws its fret number (string-colored, head styling reduced) pinned at the edge over
   the tail, clipped to the tail extent.
3. **Arpeggio posture brackets** — a span whose "[ fret ]" posture column (drawn at the span
   start) is off-screen left redraws the column pinned at the edge for the rest of the span, in
   the same muted styling, including the string-line exclusion ranges at the pinned x.

This is **view-locked** (anchored to the viewport's left edge at all times). It is deliberately
NOT plan 51's parked cursor-locked posture display (identity traveling with the playhead); do not
merge the two.

## Design

- `TabView` gains `setVisibleContentLeft(int)` mirroring the tone views, pushed from
  `TrackViewport::updateRulerView()` beside `m_tone_track_view` / `m_tone_automation_lanes_view`
  (rock-hero-editor/ui/src/timeline/track_viewport.cpp).
- All three pinned drawings live in `TabView::paint` in canvas coordinates:
  `pinned_x = max(anchor_x, m_visible_content_left)`, clipped to the owning span/tail so the mark
  slides off with its owner. Chips keep exact-x anchoring and later-wins crowding for the
  non-pinned case.
- Data is already present on `TabViewState`: `TabShapeView{start_seconds, end_seconds, name,
  arpeggio, arpeggio_notes}`, `TabNoteView{start_seconds, end_seconds, string, fret}`. The
  interval-stabbing visibility structure (prefix-max sustain ends) already finds
  tail-crosses-the-edge notes.
- Repaint discipline: start with the tone views' repaint-on-visible-left-change; if scroll or
  window-shift profiling shows the full tab repaint costing too much, narrow to old+new left-edge
  strip repaints (the `repaintCursorStrip` pattern in timeline_cursor.h).

## Non-goals / notes

- Chord-span posture pinning (fret dots for chords, not just arpeggios): chords have no projected
  posture notes today (`arpeggio_notes` is arpeggio-only); doing chords needs projection work —
  the same requirement plan 51 recorded. Possible follow-up, not part of this plan.
- Tests: pinned-chip, pinned-sustain-fret, and pinned-bracket cases in
  rock-hero-editor/ui/tests/test_tab_view.cpp.
