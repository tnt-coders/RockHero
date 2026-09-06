\page guide_2d_views The Editor's 2D Views

*Applies to: Editor-only (the string-color palette it draws with is shared repo-wide).*

The editor's center is a stack of horizontally-scrolling timeline rows — waveform, tablature,
tone track, automation lanes — under a pinned ruler, with a cursor overlay on top. This page
explains the machinery those rows share, then each row, then the checklist for adding a new row.

# One viewport rules them all

`TrackViewport` (`rock-hero-editor/ui/src/timeline/track_viewport.h`) is the single owner of
horizontal zoom (`m_pixels_per_second`) and scroll. It hosts the pinned `TimelineRuler`, the
scrolling canvas that parents every row, and the `CursorOverlay` spanning the whole stack. The
tab lane is laid out to exactly the waveform row's bounds — it draws *over* the waveform.

```mermaid
flowchart TB
    tv["`TrackViewport
    owns zoom + scroll + the one grid scan`"]
    ruler["TimelineRuler (pinned)"]
    canvas["scrolling canvas (Content)"]
    wave["ArrangementView — waveform"]
    tab["TabView — tablature (drawn over the waveform row)"]
    tone["ToneTrackView — tone regions"]
    lanes["ToneAutomationLanesView — parameter lanes"]
    cursor["CursorOverlay (on top, spans the stack)"]
    tv --> ruler
    tv --> canvas
    canvas --> wave
    canvas --> tab
    canvas --> tone
    canvas --> lanes
    canvas --> cursor
```

Three consequences keep the rows pixel-aligned:

- **One time window.** Every row maps time to pixels with the same linear function
  (`timelineXForPosition` in `editor/core/timeline/timeline_geometry.h`, or its local
  equivalent):

  ```cpp
  float xForSeconds(double seconds, common::core::TimeRange visible_timeline, int width)
  {
      const double duration = visible_timeline.duration().seconds;
      return static_cast<float>(
          (seconds - visible_timeline.start.seconds) / duration * static_cast<double>(width));
  }
  ```

  over the same range, which `TrackViewport` hands to every row it hosts
  (`TrackViewport::canvasTimeline`, pushed from the canvas layout). That range is the session's
  `visible_timeline` from the pushed `EditorViewState` **preceded by a gutter's worth of time**:
  the canvas starts a little before the timeline does, so the first beat's note head — heads are
  centered on their instant, so half of one hangs left of it — has canvas to draw its left half
  on at the leftmost scroll position instead of clipping. The gutter is the same window-pin
  fraction playback follow parks the moving cursor at, so scrolling fully left and a follow shift
  leave the working position at the same screen x. Rows do **not** read `visible_timeline` off
  the state themselves: the shell that sizes the canvas owns the range its width represents, and
  a row that took the session range instead would draw its notation a gutter out of place.

- **One grid scan.** `TrackViewport::refreshTimelineGrid()` computes `visibleTempoGridLines(...)`
  (`editor/core/timeline/tempo_grid_geometry.h`) once per geometry change and pushes the *same*
  line list to the ruler and the canvas. Rows never rescan the tempo map themselves — that rule
  is what fixed the 1/128-grid performance problem, so keep it.

- **One snap function.** `musicalGridPositionForX(...)`
  (`rock-hero-editor/ui/src/timeline/timeline_cursor.h`) converts a pixel to an exact rational
  grid position for *every* gesture — cursor placement, tone-region boundaries, automation
  points. It takes the **placement quantum**, not the grid value: `placementQuantumNoteValue(...)`
  is the one authority, and `EditorView::setState` derives it once per push and hands the same
  value to every placing surface, so no view can snap by a rule the controller did not use. No
  modifier composes a second answer — grid snap (`Ctrl+G`) is the only thing that moves it. The
  keyboard's stepping has its own single primitives next to the grid math (`gridStepBeats` and
  `adjacentTempoGridPosition` in `editor/core/timeline/tempo_grid_geometry.h` — the one
  step rule behind both the caret step and the lane nudge, exact-rational so a step from a
  position between lines lands on the adjacent one). New gestures must go through these
  helpers, or their snapping will disagree with everyone else's.

The pinned ruler stacks the **song-level** chip rows on top — sections, tempo markings, and time
signatures on the editor chrome, with the active value pinned to the left edge while the song
scrolls — above the ruler body with its measure-number row and tick band; the color steps alone
divide chrome, body, and the content scrolling under it. Each chip drops a dotted leader line in
its own color down to the top of the body, marking its exact position (the body's own ticks take
over from there); leaders draw for every event, even where a chip was suppressed on a dense map,
and every chip paints above every leader. A 1px divider along the bottom edge
separates the ruler from the rows scrolling under it.

(The ruler's bottom band used to carry chord/arpeggio NAME chips. Nothing ever authored a chord
name — the postures both surfaces draw are derived from the notes and carry no name — so the band,
its chips, and `TrackViewport::setShapeLabels` were deleted rather than kept as a row that could
only ever be empty. When names are authored they arrive as a dictionary keyed by a posture, and
the band comes back with them.)

# How rows get data: push for content, sample for live

Two channels exist, and choosing the right one matters:

- **Pushed content state.** The controller derives view-state structs and `EditorView::setState`
  fans them out — `m_tab_view.setState(m_state.tab, ...)`,
  `m_tone_track_view.setState(m_state.tone_track)`, and so on. Rows repaint when state changes;
  no timers poll for content. Large states are shared pointers compared by **pointer identity**
  (`TabView` rebuilds its index only when the pointer changes), so pushing an unchanged state
  is free.
- **Sampled live data.** Anything that moves at playback rate — the transport position, meter
  levels, live parameter values — is deliberately *not* in derived state. Views sample it
  through the const port references bundled in `EditorView::AudioPorts` at render cadence, using
  `juce::VBlankAttachment` (never `juce::Timer`). The cursor repaints only a narrow strip
  (`repaintCursorStrip`), not the whole canvas.

If you are adding something that changes when the *user edits*, push it; if it changes because
*audio is playing*, sample it.

One narrow channel runs the other way, **upward**: when the viewport needs geometry a row owns —
the armed caret square's vertical mask, which the paused-column cursor must cut around — the row
*publishes* it fire-on-change (`TabView`/`ToneAutomationLanesView::setCaretMaskCallback` →
`TrackViewport::setTabCaretMask`/`setAutomationCaretMask`). The viewport never polls a sibling
row's geometry. The reason is the memo
below: sampled-channel derivations are gated by change keys, and a polled value that changes
without a notification freezes inside the memo.

Sampled-channel work is **idle-gated**: `TrackViewport::updateRulerCursor` runs at vblank
cadence but short-circuits on an unchanged `RulerCursorKey` (`track_viewport.h`), and the
content views' `setState`/`setVisibleTimeline` equality-gate so unchanged pushes repaint
nothing. The rule that keeps the memo honest: **every input of the derived value must be a
field of the key** — the pushed caret mask is one — because a missing field freezes the output,
and a frozen derivation shows up as a *lingering* (not one-frame) paint glitch.

# One selection, editor-wide

Exactly one selection exists across all surfaces:
`EditorSelection = std::variant<std::monostate, ChartSelection, ToneRegionSelection,
AutomationPointSelection, TimeSelection>` (`editor/core/src/controller/editor_selection.h`).
Making a selection anywhere replaces it everywhere — two live selections are unrepresentable —
and verbs (Delete, Alt+arrow moves) dispatch on whichever alternative is active.

Inside the chart alternative there is a second axis, the selection **unit**: a `ChartSelection`
holds `ChartSelectionKey` values, and that key is a **sum** —
`std::variant<ChartNoteKey, ChartKeyframeKey>` (`chart_selection.h`). The first is named by a
`ChartSlotKey`, the `(position, string)` the note stream is keyed by — silently-held stops
included, since they are notes; the second is not, and that is why the key is a sum rather than a
kind tag beside a slot. A
note carries many keyframes, so a keyframe's identity is `(note slot, offset)` — the authored
beat-fraction offset and never an index, because removing an earlier keyframe shifts every later
index and moves no offset. Carrying that offset as a field only one kind uses would make "a note
key with an offset" spellable and owe every reader a rule about what it meant; as a sum neither
shape exists to be misread.

One sorted-unique sequence per alternative, one set of mutations written over whatever
(sequence, element) pair an alternative maps to — `ChartSelection::visitSequence` is that single
mapping, so a mutation never branches on kind and the keyframe sequence can hold a different
element type than the two slot-keyed ones. Every verb reads its own kind's operand as a plain
list (`notes()`, `keyframes()`) and a verb a kind has no meaning for simply reads an empty one,
which is what keeps the technique verbs free of keyframe guards.

Three consequences worth knowing before touching this:

- **A keyframe occupies no slot, so nothing arms a caret for it.** `chartCaretSlotFor` answers
  absent for a keyframe key, and selecting one demotes the marker to a cursor in place, exactly as
  every multi-select gesture does. The armed-caret invariant is "the selection is what sits under
  the caret", and a caret on the note while the selection holds the keyframe would break it.
  `chartSlotOccupied` (does the note stream hold anything at this slot) is therefore a narrower
  question than "what can be selected", and deliberately excludes keyframes: a slot holds at most
  one note, while a keyframe SHARES its note's slot.
- **Keyframes publish as drawn positions, not as chart identity.** `ChartEditViewState` carries
  `selected_keyframes` as `ChartKeyframeRef{note_index, keyframe_index}` beside the note index
  list, resolved against the presented projection the lane hit-tested; a key the trim clipped out
  of the drawn tail resolves to nothing and simply wears no ring.
- **`selection.empty()` is not "this verb has no operand" any more, and the difference bites.**
  Widening the key split one question into two: a verb whose operand is the slot-keyed notes
  (`moveChartSelection` — Alt+arrows) can see a non-empty selection with `notes()` empty — a
  keyframe-only selection — and reading a `front()` off it is out of bounds rather than merely
  inert. Every such verb guards on the operand it actually reads, never on `empty()`. Verbs whose
  planner takes the keys as a list need no change: an empty key list already means NoChange. The
  typed DIGIT is the same question one level up: it routes by whether the selection holds notes to
  retype, not by `empty()` — routing it by emptiness armed a pending entry whose target was an
  empty key set, and because an invalid entry is the one kind that outlives its window by design,
  a digit typed over a keyframe left a red box no timer would clear.

**Adding a selection kind is the highest silent-fan-out change in the editor.** Because dispatch
is `std::visit`/`holds_alternative`, a new alternative compiles clean nearly everywhere it is
forgotten. The touchpoints:

1. The variant + the new struct in `editor_selection.h`, identified by *value* (ids, exact grid
   position), never by display index, so it survives rebuild pushes.
2. Assignment through `setSelection` (the one non-chart seam — it carries the fret-entry
   invalidation invariant) and the accessors/clears around it in `editor_controller_impl.h`.
3. The verb dispatches: `onSelectionDeleteRequested` and `onSelectionMoveRequested` — a missing
   arm means Delete/moves silently no-op on the new kind.
4. An Esc-ladder rung (\ref guide_keyboard).
5. The `selection_present` derivation in `deriveViewState()` — the view's Delete/Esc guards
   read this one flag.
6. **The lifecycle rules** — the subtlest step. Each kind declares what clears it: on play, on
   seek, on cursor move, on project load/close/arrangement switch (the per-kind split is
   documented at the top of `editor_selection.h` and in `clearCursorCoupledSelection`). A kind
   that forgets to pick dies stale on screen.
7. The view-side highlight render, and tests covering the dispatches plus the lifecycle clears.

The `TimeSelection` alternative (Shift+arrows, 2026-07-20) is the newest worked example: a
grid-locked anchor/focus span, mutually exclusive with object selection by construction, whose
creation demotes the marker to passive through the seek-preserving dissolve
(`dissolveChartCaretInPlace`) — building a range and pressing Space plays from the range.

# The rows

## Waveform — `ArrangementView`

The waveform is drawn by the audio engine, not by editor code: `ArrangementView`
(`ui/src/timeline/arrangement_view.cpp`) owns an `IThumbnail` created through the
`IThumbnailFactory` port, and its `paint` hands the clipped time range straight to the port:

```cpp
m_thumbnail->drawChannels(g, request.bounds, request.visible_range, vertical_zoom);
```

`IThumbnail` (`rock-hero-common/audio/.../song/i_thumbnail.h`) is the one port whose signature
deliberately names `juce::Graphics` — it forward-declares it so callers can draw without any
Tracktion header. The production adapter (`src/tracktion/tracktion_thumbnail.cpp`) wraps
`tracktion::SmartThumbnail` and translates the time range; proxy generation is asynchronous, and
the view renders progress text until it completes. Two details worth knowing: drawing shifts by
`state.audioStartOffsetSeconds()` so asset time aligns with timeline time, and `vertical_zoom`
comes from the asset's normalization gain (`pow(10.0, gain_db / 20.0)`).

## Tablature — `TabView`

`TabView` (`ui/src/tab/tab_view.cpp`) **owns its pointer events while a chart is displayed**: it
claims the whole lane band through `wantsPointerAt` / `hitTest` and forwards Down, Drag, Up, Move
and Exit to the controller as `ChartPointerEvent` intents, plus a right-press context menu; the
controller decides what a press means (select, caret arming, marquee, or a plain seek while
playing). With no chart the lane is pointer-transparent. One column of the claimed band answers
nothing: the string legend's and the fret-hand chip pinned on it, which are inert chrome (see
"The pinned chrome is INERT" below). The yielding component is the *cursor
overlay*, whose `hitTest` returns false wherever a pass-through predicate — installed in
`editor_view.cpp`, asking `TabView::wantsPointerAt` first — declines the point. Its data is a
seconds-resolved projection built once
per edit in **common/core** (`chart/chart_projection.cpp`,
`common::core::makeChartViewState(arrangement, tempo_map)` — the ONE chart scene both surfaces
draw since W9-B's fold on 2026-08-21; promoted from editor/core by plan 30
Phase 1 so the game's 2D tab view shares the same scene model), so painting never queries musical
positions. Because sustains overlap, it keeps a prefix-max index
of note end times and binary-searches the visible note range each paint instead of scanning the
whole chart. It draws each hand-shape span's rail and, for an arpeggio, its brackets; a span's
NAME has no drawn form on either surface, because nothing authors one (see above).

String colors come from the **shared palette** in
`rock-hero-common/ui/string_colors/string_color_palette.h` — a JUCE-free authority (colors are
`uint32_t`) that derives each string's seven surfaces (lane, borders, tail, accent...) from one
base color, Charter-style. The 2D tab lane, the 3D highway renderer, and therefore both products
all color strings through it. The glyph renderer itself is the **shared notation paint core**
in `rock-hero-common/ui` `tab/` (plan 30 Phase 2): `tab_lane_layout.h` holds the framework-free
`TabLaneGeometry` and lane math, `tab_layout_manifest.h` answers "where is this note's head
in pixels" for hit testing (and the same for a linked keyframe's head, and for a silently-held
stop's **posture bracket** — such a note draws no head, so what the editor marks and hit-tests is
the arpeggio bracket the paint core already draws, at the instant the derivation publishes for it
(`ShapeViewState::bracket_seconds`, resolved from `ChartShape::bracket_position`: the span's own
FRONT for every span an EVENT states — which for an accumulation is its earliest uncovered member's
onset, not whichever arrival reached the threshold — and the first interior sounding for a
landing-opened one), and the bracket's size lives
on `TabLaneGeometry` for exactly that reason: the painter and the hit test read one authority. A
selected hold wears the accent on that bracket's own SILHOUETTE (`strokeTabBracketOutline`, user
ruling 2026-08-27) — down each bar and around its serifs, and on to the satellite column when the
digit was displaced there — rather than as a box around the pair, whose top and bottom edges ran
straight through the empty centre and read as a ring around nothing. A
hold that joined no posture draws no bracket, and the layout answers with no box at all, so
nothing undrawn is clickable without a second rule saying so; and the same again for a **held stop's
satellite** — the digit column outboard of a bracket's closing bar, where a right-hand onset prints
what the fretting hand is holding while its own head prints what the picking hand sounds. That
column's width lives on `TabLaneGeometry` too, and it is derived from the lane's text scale rather
than measured from the digits, which is exactly what lets the framework-free layout bound the mark
the painter draws. It is an independent TARGET: clicking it selects the note and pre-arms the
held-stop entry, so the digits that follow state that stop.

WHICH column a posture digit lands in is no longer the painter's derivation: the projection
publishes it per posture string (`ShapeStringViewState::digit`), and publishes each claim's own FACE
beside it (`NoteViewState::stop_mark` — a hold's bracket, a fronting tap's displaced digit, or a
note's own satellite), so the painter draws where the hit test looks (user ruling 2026-08-27). **WHETHER one lands at all is asked AT THE MARK'S OWN INSTANT and at no other — THE
DIGIT WINDOW** (user ruling 2026-08-31, the review-blocker walk). One head can stand on the string
there, and the three answers are one question about it: the bracket's centre where NOTHING heads
the string or a fretting-hand head there prints ANOTHER number; the satellite column where a
RIGHT-hand onset there prints another number; and nothing at all where a head there prints THIS
one, which is the only thing suppression exists to prevent. THE FRET IS PART OF THE TEST on every
arm. A head LATER in the span suppresses nothing: the opening bracket is the span's CHORD FRAME, so
it states the whole membership where the reader meets it and an accumulation's members print their
frets there, their own heads restating them as they arrive. Asking over the whole SPAN — which is
what stood here — emptied that frame of everything still to come, and its inclusive end let the
onset that CLOSED the span decide the digits inside it. That closed the drawn-digit-clicks-nowhere
gap — a HOLD's own digit can be
displaced into the satellite column by a right-hand onset at the bracket's own instant, wherever
the derivation anchored it — which is not the span's front when a LANDING opened that span —
and out there it used to belong
to no target at all. Now the hold's own box runs out to cover the column its digit was drawn in, so
the digit selects what the bars select and nothing past the drawn column is reachable),
and `tab_paint_core.h` — the one
designated juce_graphics-bearing
common/ui header — exposes `paintTabLane` and `paintTabLaneFurniture`, which `TabView::paint`
calls after deriving metrics. They are **two passes because a host puts chrome between
them**: `paintTabLane` draws the lane's CONTENT (the marks standing for chart events at their
own instants) and `paintTabLaneFurniture` the marks stating what is IN FORCE across a stretch
— the hand-shape rails, the capo chip, the fret-hand chips. A host with nothing to interleave
calls them back to back.
The editor keeps thin delegate functions (`tabStringColor`, `tabLaneCenterY`, ...) on its own
surface so editor widgets and tests are unaffected; the paint core's pixel output is pinned by
exact-color tests in `rock_hero_common_ui_tests`. Those delegates carry no documentation of their
own rules — `tab_view.h` points at the shared declarations instead, because a delegate that restates
the rule it forwards gives the reader two descriptions to reconcile and no compiler to catch the
drift.

Six notation rules inside the paint core are worth knowing before touching a head, because each
is deliberately single-sourced:

- **The head silhouette names the note's kind**, never which hand produced it (a present mark's
  *darkness* says that). `headShapeFor(note)` maps to `HeadShape::{Round, Diamond, Plectrum}` — a
  diamond for anything carrying a harmonic node, a plectrum for a scrape, a circle otherwise. The
  enum is file-local on purpose, so host chrome that must trace a head it did not draw calls the
  exported `strokeTabNoteHeadOutline` instead: re-deriving the rule in the editor left every pick
  slide wearing a circular selection ring around a plectrum head. Its bracket twin
  `strokeTabBracketOutline` exists for the same reason and reads the same columns the bars are
  filled from (`TabLaneGeometry::bracketColumnsAt`), so a selection edge cannot miss its bar.
- **`tabNoteHeadText(note, fret_at_head)` decides the number a head carries**, and it takes *the
  stop being labeled* rather than reading the note's own fret. Any harmonic whose node is on the
  neck names its node — the predicate is `nodeIsOnNeck`, which excludes only a pinch — because the
  node sets the pitch (a trailing `.0` is dropped so 12 / 7 / 5 stay as narrow as an ordinary
  fret, and the pinch keeps its fret because its node sits off the neck).
  Passing the stop is what lets one rule label *every* head of a gesture: the onset passes
  `note.fret`, a linked slide junction passes the fret the glide has reached, so a harmonic labels
  nodes at all of them instead of a node at the onset and a raw fret at the junctions.
- **The capo is drawn**, as a "Capo N" chip pinned in the lane's top-left corner in the fret-hand
  chips' boxed style — pinned to the bounds rather than the timeline, because a capo has no time.
  The chart stores absolute frets with 0 meaning the capo'd open string, so nothing else in the
  drawn content says where the string floor sits. Crude first treatment (roadmap 25-Q6).
- **The beside-head legato triangle comes from the note's RESOLVED motion**, never from a stored
  direction: `NoteViewState::legato` is a `LegatoMotion` the projection got from `chartResolutions`,
  and the painter simply points the triangle down for `Hammer` and up for `Pull`. `Unjustified`
  draws nothing, so a claim the chart cannot justify is pixel-identical to a plain pick —
  deliberately, per `docs/plans/in-progress/legato-authoring-model.md`. Nothing in the paint core
  knows the rules that produced the value.
- **A stored `LeftTap` is the one exception: it wears its own charting mark** — the tap letter on
  the LIGHT plate (ruled 2026-08-12). The lettered-plate family's hand signature is its FILL
  POLARITY (55-Q1's basis: dark ink marks the picking hand, light the fretting hand), so the
  right-hand tap's dark T and the left-hand tap's light T share a letter without colliding; one
  shared mid-grey rim (`g_plate_rim`, perceptually equidistant from both fills) keeps the two
  polarities at equal visual weight. This is a CHARTING mark — it states how editor verbs treat
  the note, not how it is performed — which is the ruled reason it exists in the editor's 2D lane
  only: the 3D surfaces keep the merged hammer-motion reading, and the game's future 2D tab view
  must suppress it (recorded in roadmap plan 30).
- **A tail's end is the note's own presented end (`NoteViewState::end_seconds`), everywhere.** The
  lane draws to it, the visible-range prefix maximum indexes it, and both paint passes cull on it —
  one stop, read off the note, so a drawn ribbon is always in range. There is no second end to keep
  in step, which is the point: a span-extended ribbon once shipped drawn-but-invisible because the
  places that had to agree did not.
  Its START is always the note's own onset, and there is nothing else to consult: every note's tail
  draws, unconditionally, to that one end. **SPAN FURNITURE MAY HIDE A TAIL, NEVER SHORTEN ONE**
  (the tail law, user ruling 2026-09-04; binding re-scoped by the execution-form amendment,
  2026-09-03) — where a member's OWN SPAN covers its whole ring, the core presentation
  (`common::core::presentedChartNotes`) MARKS that tail hidden without emptying it, so this lane
  simply draws it: the lane shows the execution form always, and the amendment is why — the lane
  is the charter's exact-duration surface, and every hidden-tail peek this paragraph used to need
  (the crude stub at each hidden head included) died with the population. The law is verdict-only
  and class-blind: it assigns no length, so every ribbon here is exactly the picture the chart
  would draw with no furniture at all — a bracket over a DRY arpeggio shows its real stepped
  rings, priced and ruled (the covered form, 2026-09-04). The VERDICT rides the projection beside
  the end (`NoteViewState::rested`) for the HIGHWAY's sake: the board rests these ribbons at
  distance and draws each only inside its curtain — the fixed one-lead window at the hit line
  and, in flight, an identical head-anchored local copy fading in across the approach (the
  tunable `g_tail_reveal_lead_whole_note`; user design 2026-09-06) — the per-surface split the
  amendment deliberately adopted, structure at reading distance there, full duration ink here.
  What this replaced, twice: C3, an ink-ownership rule with a per-note `tail_suppressed` flag that
  this lane and the 3D board each tested at their own draw sites — the one shape in which two
  surfaces could disagree about a tail — and then the bracket law's staircase, which CLIPPED a
  covered ring at its next head and so made one ribbon's length a function of a neighbour's
  position. The presented end is still the whole answer, so the prefix maximum, both culls and the
  future scorer all measure exactly what is drawn, and the actual-ring reveal still shows the stored
  ring the span is carrying (nothing is hidden in that form).
  The span-implied hold (`ChartViewState::display_hold_ends`) still rides the same projection, but
  it is the **3D board's** — how long a pinned head lasts — and this lane must not spend it
  (ruled 2026-08-22, `docs/plans/in-progress/note-sustain-model.md` ruling 3). A chugged member of
  a strum a hand-shape span holds therefore draws a bare head here and no ribbon: the chord box over
  the strum already says how long the shape stays fretted, and repeating that in the one mark that
  means "this string is still ringing" read as sustain. The board has no chord box, so pinning its
  heads is how it states the same fact. One chart, one hold, two idioms.

**THE STRING LEGEND** (user ruling 2026-09-03, amended twice) names the lines: each string's
own open-string pitch ("E2", "A2", "D3" — `ChartViewState::open_strings`, which is the chart
tuning's array verbatim, so a drop or altered tuning prints what it named), inked in that string's
own colour and sitting ON that string's line at the fret digits' size, inside one panel pinned at
the window's left edge. It answers "which line is this string?" wherever the lane is scrolled to
and not only where the lane happens to be empty.

**The panel is an EXCLUSION plus a TINT**, which is the second amendment and the whole of its
current design. It was a scrim laid over finished notation; what stood under the letters was then a
quieted stretch of chart nobody could decode, and it also hid the *waveform* the canvas paints
beneath the lane. So the column is now composed rather than covered, and every layer is stated once
in `TabView::paint`:

1. **The tint** (`drawTabStringLegendTint`) goes down first, over whatever the canvas painted —
   the waveform — and under everything this lane draws. `g_legend_scrim_opacity` is still the
   sighting knob and still carries its name, but it now moves exactly one thing: how much of the
   waveform the column shows. At full strength the column reads as an opaque stretch of the row
   band, which is what the ground it replaced read as.
2. **The lane's content is excluded** from the column — ONE
   `juce::Graphics::ScopedSaveState` + `excludeClipRegion` around both `paintTabLane` and this
   view's own editing overlays. Notation there is *absent*, not quieted, at every knob setting.
   This replaces the string LINES' own exclusion inside the paint core: that rule ("a mark whose
   whole content is its position says nothing faintly") turned out to be true of every mark drawn
   under the letters, so `TabLaneMetrics::legend_panel` is gone with it and the paint core no
   longer knows the panel exists.
3. **The furniture draws OVER the panel** (`paintTabLaneFurniture`): a hand shape running under the
   column is still in force there, and a rail cut out of it would say the shape had ended.
4. **The governing fret-hand chip** stands on the panel — see below.
5. **The letters last**, over all of it.

The **canvas beneath stops its grid at the same column**: `TrackViewport::Content::paint` excludes
the panel from `drawTempoGridDots`, so the names never stand on a field of dots at any tint
setting. It PULLS the rectangle from `TabView::legendBounds()` rather than taking a push — the
panel is pinned to the window and moves on every scroll, and the lane already invalidates the
column it leaves and the column it takes; a transparent child's repaint reaches the canvas, so
those two strips are repainted here with the panel where it now is.

**One width authority.** `tabStringLegendBounds` measures the widest note name the display could
ever state — every letter, in both accidental spellings, with an octave digit — so retuning a song
cannot move the panel and neither can scrolling into a chart spelled differently. That costs a few
pixels against measuring the tuning at hand and buys a panel that never moves under the reader. It
is 210 text layouts, so the size is cached (`TabView::refreshLegendColumn`, re-derived only when
the bounds, the projection or the lane count change) and never asked on the paint path. Every
reader of the column — the exclusion, the tint, the letters, the scroll repaint, the hit test and
the canvas's grid — reads that one rectangle.

The panel is **screen-pinned**, not canvas-pinned: `TabView::setVisibleContentLeft` takes the
viewport's left edge from `TrackViewport::updateRulerView`, the same push the tone rows already
take, so the letters live over the origin gutter at rest and stay at the window's left edge while
the follow scrolls the canvas under them. The scroll repaint is held to the column the chrome
leaves and the column it arrives in — the viewport blits the rest, and a per-frame full-row repaint
would re-rasterize the whole visible chart for one column of chrome.

**THE GOVERNING FRET-HAND POSITION PINS THERE TOO** (user ruling 2026-09-03), which is what makes
the panel a *current-state column* rather than a name column: which line is which string, and where
the hand is. An FHP is a region-scoped value exactly like a tempo or a time signature, so the
placement governing the view's left edge stands at that edge and **yields** as the next placement's
own chip scrolls in — the pin is dropped rather than the incoming chip suppressed, so the new value
scrolls on to the edge and takes over.

That yield law is the timeline ruler's, and it now lives in one place for both: `sticky_label.h`
holds `pinYieldsToIncomingLabel` beside `stickyLabelLeft`, two DIFFERENT laws kept together so a
reader reaching for one can see the other is not it — `stickyLabelLeft` is geometric (a label rides
its anchor and sticks at the window's edge while any of that anchor is on screen), the pin law is
about succession. `g_pinned_label_gap` is the clearance both spend, so a ruler row and the tab lane
can never end up disagreeing about how close is too close. The ruler's four value rows call it
(`timeline_ruler.cpp`); `TabView::refreshPinnedFhp` is the other caller.

The chip itself is the **ordinary marker chip**, drawn through the one authority every scrolling
placement draws through (`drawTabFhpChip`, given the pin's column instead of its own) with its
geometry from `tabFhpChipBounds` — the pin needs the width *before* it draws, because the yield
boundary is that width plus the clearance. `refreshPinnedFhp` re-derives which placement governs
when the WINDOW moves, not per paint, and resolves it in columns rather than seconds so nothing has
to invert the lane's time-to-x mapping.

**Text sits on a line by its INK, not by its font's line box.** JUCE centres text by the font's
ascent-plus-descent box, and everything this lane prints — fret numbers, node labels, bend amounts,
string names — lives between the baseline and the cap line with no descender ink at all, so a
line-box-centred number asks for a baseline `(ascent - descent - ink height) / 2` too low. The
software renderer then rounds that to a WHOLE ROW (`juce_RenderingHelpers.h`, `drawGlyph`), so a
third of a pixel becomes a full one: at the shipped 12.5 px fret font the digit was drawn with 2.98
px of ink above the string line against 4.50 below it. `TabLaneFont` is the one authority that fixes
it — a lane font paired with a correction measured once from a reference figure's real outline, with
`draw` the only way text reaches this lane, so no drawer can take the font without the rule. The
same measurement (`TabLaneFont::inkHeight`) is what the T/S/P plates and the attack marks' `tuck`
floor keep clear of.

**The pinned chrome is INERT**, which is the pointer half of the same ruling. It stands permanently
over one column of notation, so a press there would select, drag or insert on marks the reader
cannot see, and a hover would arm the insert ghost behind the letters. The lane still CLAIMS the
column — `wantsPointerAt` is unchanged, so the cursor overlay keeps passing the press down and no
click-to-seek fires under the letters — and simply answers it with nothing: `wantsNotationAt`
(`wantsPointerAt` minus `pinnedChromeBounds`) is what the press and the hover ask, and a hover over
the column forwards `Exit` so the ghost clears exactly as it does when the pointer leaves the lane.
The question is asked of the panel UNITED with the pinned fret-hand chip, because a wide placement
spells out its range and its chip reaches past the panel's own edge; one rectangle answers both
"what does a scroll repaint" and "what does the pointer refuse". It is
the tone row's chip rule read from the other side: a mark drawn ON TOP of a target resolves the
pointer that lands on it, and this mark has no menu to open, so its answer is silence.

**HEADS ARE TARGETS; TAILS ARE TESTIMONY** (user ruling 2026-08-30), and that is the lane's whole
hit model. What a press can select is a mark drawn at the instant the thing it stands for happens:
a note's head, a silently-held stop's posture bracket, a held stop's satellite column, a linked
keyframe's head. A tail selects nothing at all, and the rule is UNIFORM — a plainly visible ribbon
as much as one a covering span's furniture HIDES — so a press over a ribbon resolves to no
note and falls through to what a press on bare lane area has always done: seek, and arm the caret
at the slot under the pointer. The reason is the armed-caret invariant itself, "the selection is
what sits under the caret": a mid-tail click selected a note whose onset was somewhere else
entirely, and a selection standing at a spot where the note does not HAPPEN is not under the caret
in any sense the rest of the editor means. The tail rectangle the layout manifest used to publish
went with the target rather than being corrected, because it was the one rectangle in that manifest
that did not bound what the lane draws — it spanned the whole presented ring while a member under a
span's ink draws no ribbon at all — and retiring the target deletes the divergence instead of
maintaining a
correction to a rectangle nothing is allowed to resolve against. The affordance this costs is
selecting a long sustain whose head has scrolled out of view by clicking the part of it you can
still see; the marquee and keyboard selection both still reach such a note, and the loss is
recorded as a sighting item in `docs/tracking/watch-items.md` rather than pre-emptively patched.

**WHERE A SATELLITE STANDS, and what a press on one addresses** (user ruling 2026-08-31, the
review-blocker walk, final law). A satellite is the note's held FACE, note-scoped, at the note's own
slot — and whether it stands is a question about AUTHORSHIP rather than about where in a span the
note sits. An **authored** held stop earns standing ink wherever it lies, mid-span and span-less
alike: an authored statement is the charter's, and nothing else in the picture prints it. A stop a
PULL-OFF **derives** is already printed by that notation, so it does not stand; it is **revealed**
on the note's own truth channel — visible exactly while the note's real ring is, which is the
selection-and-reveal pick the lane already makes. Revealing a note shows the whole truth about it at
once. And a **tap fronting a bracket** stands whatever its authorship, because there the bracket
owes the statement: the tap's head holds the string's centre, so the posture's digit is displaced
into the satellite column and IS that tap's face ([D2]).

**AND EVERY RIGHT-HAND ONSET HAS ONE, because every one of them has a held stop** (user ruling
2026-09-02, THE DEFAULT HELD FACT). A tap that states nothing — no authored field, no pull-off to
derive one — is not a tap with no fretting hand under it; the hand is holding whatever grip it is
holding, so the release lands on the **covering span's posture fret for that string**, or on **0**,
the open string, where no span covers the tap or the posture names no fret there. It is
LIVE-DERIVED off the postures, so an edit that reflows the spans moves it. Its face follows the same
authorship rule as a derived one — **revealed**, because it is not the charter's ink — but it is the
opposite of read-only: nothing owns a default, so typing at that satellite AUTHORS a real held stop.
That is the one revealed satellite a digit lands in, and it is why the held channel now reaches
every right-hand onset rather than only the ones carrying a stored field. A default wears the note's
OWN satellite column even where the bracket beside it prints the same number — the two are different
statements about one fret.

**Two facts, two inks, for a mid-span tap.** Its fret prints in the opening bracket as grip
MEMBERSHIP — the digit window, unchanged and independent — and its satellite beside its own head is
the note's own face, what a press addresses and a typed digit retypes. A derived satellite is
read-only: the derivation owns the stop, so the retype verbs refuse it in red rather than quietly
landing the digit on the sounding fret beside it. The refusal keys on the **pull-off derivation's
presence** (`ChartResolutions::derived_stops`) and never on the face or on the held field being
there — which is what keeps it off a default, whose satellite wears the same revealed face and
accepts the digit.

**SAME-FRET SETTLE** (user ruling 2026-09-03). Typing the value the derived satellite ALREADY shows
is not an authoring attempt, so it is not refused either: it asks for the state the chart is already
in, and settles as the no-op it is — no red, no authored field, no undo entry, the pending entry
just closing clean. Only a DIFFERENT digit is the charter contradicting the notation, and that still
refuses. In a multi-note entry it is which members are refusal CAUSES that changes, never the scope
of a refusal: a disagreeing derived member still rejects the whole plan, an agreeing one simply
drops out of it, and the entry's default and authored satellites are written as ever.

**One reveal, one predicate.** `core::chartNoteRevealed` (editor core) is the whole of it — the lane
reveal modifier, the selection, or the caret standing inside the note's stored ring — and everything
downstream reads that one answer: which form the note draws in, whether the paint core draws its
satellite, whether the layout manifest bounds a click target for it, and whether the caret may sit
on the held channel there. The projection stays selection-agnostic: it publishes the face and its
terms (`common::core::StopMarkFace`), and the editor layers apply the reveal.

**And a second predicate for the other subject: SPANS** (`core::chartSpanRevealed`, user ruling
2026-09-04). Rule 12a stops a span's rails one minimum-sustain-distance margin before the head that
closed it, so the drawn extent is short of the musical close by design; while the reveal is held, or
while the selection holds a note the span covers, that span's furniture runs to the close instead.
Two grounds rather than the note's three, and the missing one is the caret: the peek asks "is
something here?" about one note on one string, which is not a question a span can answer. The visual
language is the note reveal's exactly — the same ink, simply reaching further, snapping back when
the ground goes away — because a reveal shows the truth in the notation's own terms rather than
annotating it. What makes it a second predicate rather than a second arm of the first is the datum:
a note's truth lives in the OTHER projected form, so the lane hands the paint core a whole note
(`common::ui::TabDrawnNote`), while a span's two ends ride one state and the lane hands over the
answer alone (`common::ui::TabRevealedShape`, read by `paintTabLaneFurniture`). Spans are not
selectable in their own right yet; that arrives with the span-marker work.

**SATELLITES ARE NOTE-SCOPED, ALWAYS** (user ruling 2026-08-31, amending the same walk's first
reading). A satellite is its note's held FACE and nothing else: a press on one addresses that note's
held stop, whatever the selection happened to be. The dual-scope reading tried the same day — an
unselected satellite acting as the bracket's displaced digit and writing through the whole span —
was withdrawn with the verb it served, and the silently-held stop's bracket face stays the record's
own note-scoped handle exactly as it was. **SELECTION HANDLES** survive it: a selected note's
satellite is hit-tested as PART of that selection, so pressing it moves the caret onto that note's
held stop and leaves a wider selection standing — naming a stop inside a selection must not be the
thing that takes the selection away. A press on an unselected note's satellite is the ordinary
press: the note becomes the selection, with the caret on the stop that was clicked.

**SPAN-WIDE FRET EDITING IS DEFERRED**, and the reason is the keystroke it collided with: typing a
number over a bracket already means INSERT A NOTE at the caret, so a bracket-digit write-through
would have to steal it, and the dual-scope machinery existed only to decide which of the two a press
had meant. It is re-queued for the future TEMPLATE EDITOR, where a span's grip is edited as a grip
and nothing competes for the digits (`docs/plans/todo/span-marker-redesign.md`). Bracket column
digits are therefore not hit targets at all today; a digit belonging to a member that accumulates in
later is READ-ONLY notation, reachable through that member's own head.

One performance rule sits beside the viewport-bounded note range: the two **wavy tail overlays**
(the tremolo band and the vibrato sine) generate only the stretch of a tail the clip can show, via
`visibleTailRun`. Both are functions of the distance from their own **start** — the onset for the
tremolo band, the region's own start for each vibrato sine — so a clipped run lands the identical
shape (phase never depends on where generation began), and each generator snaps its run outward
onto its own vertex spacing, so the rasterized result is *identical* rather than merely similar. At
full zoom a held tremolo chord would otherwise cost tens of thousands of off-screen vertices every
frame. A test pins that a tail looks the same however the repaint is clipped.

The sine is drawn **once per stated vibrato region**, not once per note: the vibrato channel holds
from each statement until the next, so `NoteViewState::vibrato` is a list of
`{start_seconds, end_seconds, state}` regions the projection derives from the note's keyframes
rather than a flag (`docs/plans/todo/unified-waypoint-model.md`). A shake that begins where a glide
arrives — the corpus's commonest vibrato figure — therefore inks only from that arrival, and a
note that simply shakes end to end yields one region covering the whole presented tail, which is
the picture the lane drew when the channel was a single boolean. The 3D board reads the same
regions, so the two surfaces cannot say different things about where a shake starts.

Each region also carries the WIDTH it was stated at, and the sine's swing comes from that: the
ordinary (narrow) tier draws at half the swing the tail's technique band allows and the wide tier
fills it, which is `g_wide_vibrato_swing_multiplier` read in both directions from one constant. The
lane cannot simply scale the wide tier UP the way the board does, because that band is a hard clip
here — a taller wave would truncate its crests and read as a square wave rather than as a wider
shake — so the ordinary tier is the one that leaves room. A step from one width to the other is two
regions meeting at an instant, so the wave changes height where the chart says it does.

**The actual-ring pick** is how the length you cannot see becomes visible while you author it. The
lane draws presented tails, so the ring a note actually sounds for — what `Alt`+wheel edits — is
invisible wherever presentation trimmed or dropped it. One rule decides, per note, which form that
note is drawn in, and it has three inputs: a note draws its **actual** ring when the whole-lane
`Alt` reveal is held, **or** when that note is **selected**, **or** while the **caret** stands
anywhere inside that note's real ring; and its presented tail otherwise.

The three inputs answer three different questions, which is why all of them exist:

- **The selection is the note under scrutiny.** You selected it to do something to it, so its real
  length is what you need to see — and every chart verb already settles on a selection change, so
  deselecting *is* the moment presentation clips the tail back to the picture.
- **`Alt` is the lookahead, and the selection cannot serve it.** With a selection standing, typing
  a digit RETYPES those notes instead of inserting one, so a charter placing notes holds no
  selection at all — and placing the next note is exactly when the real tails around it matter.
  Holding `Alt` shows every ring in the passage, including the notes nothing is selected on.
  `Alt` is also already the authoring gate — it is what the sustain wheel gesture rides — so you
  see the ring while you are the one changing it.
- **The caret's PEEK is what a click on hidden ink means**, now that tails are not targets. The
  click does what every lane click does — it moves the caret to the slot under the pointer — and the
  note ringing under that slot draws its whole ring for as long as the caret stays in it: **the
  peek means the caret stands in this note's ring, so you see the whole ring.** Deterministic and
  keyed on the edit position alone: no timer, nothing latched, and no selection touched, so the
  caret moving away is the whole of what hides the ink again. The rule is as simple as that
  sentence (user ruling 2026-08-30, final): **if a note's stored duration says it sustains at the
  caret at all, it peeks** — onset through actual end, both ends included. Presentation is not an
  input at all: not why the ink is missing, and not where the drawn ink stopped. The warrant is
  authoring — typing a technique onto a resting tail is legal and keeps its stated portion always
  visible (a statement still running at the ring's end keeps the whole ribbon standing), so
  authoring has to function identically anywhere in the ring. Including the *drawn*
  stretch costs nothing, because the drawn part re-draws identically in either form; what you see
  is the clipped end growing into view, which is the thing you were asking about. That is what
  makes a quarter-note tail clipped a sixteenth by the next onset answer from anywhere along it
  rather than only from the sliver past its ink. And it is a
  third DISJUNCT of the rule above rather than a mechanism of its own — the same `drawn_note` pick
  in `TabView::paint`, which every overlay reads too, so nothing can trace a head the lane did not
  draw.

**The mark is the notation itself** (ruled 2026-08-23, after sighting it against the alternative).
A note drawing its actual ring is drawn in the chart's ACTUAL form: its tail is the real ring, with
its techniques and its payload riding it. Nothing is annotated, because the notation *is* the
answer. The candidate it beat — a hairline outline at tail height over the presented picture — is
deleted, with its `F6` style toggle and the `ActualRingRevealStyle` enum that carried the choice.

Two glyph consequences follow from drawing a form no presentation rule touched, and both are
accepted: a **dead note grows a tail** (rule 4 is a presentation rule, and the actual form has no
rules), which reads as how long the mute is held; and a shift-slide's arrival, which sits exactly at
the presented end, sits strictly inside the real ring, so it draws the **linked continuation head**
it never draws otherwise.

Five things about it are deliberate:

- **Every note the pick names, not only the disagreeing ones.** A note drawing its actual form
  whose ring and presented tail coincide simply looks unchanged — which is the statement "this is
  the whole ring". A mark that appeared only on disagreement would leave a reader unable to tell
  agreement from a pick that is simply not asking.
- **It needs a second PROJECTION, not a swapped end.** `EditorViewState::tab_actual` is the same
  chart through `makeChartViewState(..., ChartNoteForm::Actual)`, published beside `tab` under the
  same memo key. A view-side end swap was the obvious cheaper move and is wrong: the presented state
  has already CLIPPED the payload points its trims removed, so a bend curve or a trailing keyframe
  that left with the tail cannot be put back by lengthening it. The two forms differ in `notes` and
  in nothing else — holds, spans and their arrival kinds, fret-hand placements and their approach
  ramps, the string count and the capo are all derived from the presented stream in either form, so
  choosing per note moves no other mark on the lane, and the two forms align by index because
  presentation returns one note per note in the same order. It costs a second projection per chart
  revision, which a sustain gesture bumps per wheel notch; the reason it is not built lazily is
  that a lazy build would require the controller to know the reveal is on, which the design
  forbids.
- **The actual form is editor-only, and the producers say so.** **Scored = presented**
  (`docs/plans/in-progress/note-sustain-model.md` ruling 4) holds structurally rather than by
  discipline: `makeHighwayViewState` composes the projection with no form argument, so no board,
  game or scorer state can be anything but presented, and `ChartNoteForm::Actual` is unreachable
  from them. `chart_projection.h` is the one authoritative statement of that; this is a gloss.
- **A revealed ring is not hit-testable.** Hit testing, selection, marquee and `Alt`+click insert
  all resolve against the presented projection the controller published (`displayedTabProjection`),
  so nothing a revealed ring reaches past its presented end can be clicked, boxed, or landed on.
  The tail itself needs no such argument any more — no tail of either form is a target — but the
  marks riding one still do: the actual form restores the linked keyframe heads presentation's trim
  clipped out, and those are heads, so resolving against the presented projection is what keeps a
  head only the pick draws from selecting a key the presented lane does not show. `Alt`+wheel is
  unaffected
  because it acts on the selection, not on what is under the pointer. Inside `TabView` this needs
  no enforcement: every note paint reads comes from the one pick lambda, and the only projection
  reads outside paint are the string count and whether a chart exists, which are identical in both
  forms.
- **One conservative cull index, because a chord can be half revealed.** `TabView` keeps a single
  running maximum of the ACTUAL form's note ends and culls both forms against it. Presentation only
  ever trims, so every presented end falls at or before its own note's ring: the actual ends bound
  whatever is drawn, a ring outlasting its tail stays in range for as long as it is drawn, and the
  paint pass drops each note whose DRAWN end really precedes the window. A per-form table could not
  be used at all here — one member of a chord can draw actual while its neighbour draws presented.
  The cull runs inside `paintTabLane`, and the pick reaches it as a per-index accessor
  (`common::ui::TabDrawnNote`), so there is no second loop and no editor ink at all, which is the
  whole economy of making the ring be the notation. A second accessor beside it
  (`common::ui::TabRevealedNote`) carries the same pick's other consequence — a reveal-only
  satellite is drawn exactly while its note's ring is — so both come off ONE predicate in the view
  and this core is told the answer rather than the reason.
- **A second running maximum, over the SPANS.** The two span passes — the bracket marks in
  `paintTabLane` and the shape rails in `paintTabLaneFurniture`, which are drawn either side of
  whatever chrome the host lays between them — face the same problem the notes do and it has the
  same answer:
  nothing orders spans by END, so a span that opened off-screen can still cover the window, and
  without an index those passes started at the first span in the song and walked the whole prefix
  on every repaint. `TabView` builds it beside the notes' table (either projected form serves, since
  the forms differ in their notes alone) and hands it in. An EMPTY table is legal and means exactly
  what it used to do: the index only ever tightens the range's start, never changes which spans
  draw, so each pass still tests its own span the way the note passes do. It is built over the
  spans' MUSICAL CLOSES for the notes' table's reason exactly — the reveal can run a rail out to
  the close, and a table on the drawn extents would cull away a rail still on screen — and the end
  is NAMED at each call site (`std::views::transform`) rather than taken off the events, because a
  span carries two ends and letting a table pick by field spelling is how a cull comes to disagree
  with a paint. The 3D board names the drawn extent there, since it reveals nothing.

The key itself never reaches the editor core. The reveal is on exactly while this process is the
foreground application AND `Alt` is physically down — `juce::Process::isForegroundProcess()` and
`juce::ComponentPeer::getCurrentModifiersRealtime().isAltDown()`, both process-wide OS queries —
and `EditorView::syncActualRingReveal` hands that conjunction to `TabView::setActualRingReveal`,
which repaints only on a change. It is read from one place, the editor view's per-frame vblank
attachment — the one that already samples the meters and the time readout, for the view's whole
life — and from nothing event-driven: JUCE delivers modifier callbacks by pointer position and
per-window focus, which is exactly the axis the rule must ignore (the editor window or the 3D
preview being active both count, and where the pointer sits never matters), so the per-frame
sampler is the only one that cannot be wrong about where the pointer is. \ref guide_keyboard has
the rule and the facts behind it.

## Tone track — `ToneTrackView`

Renders the gap-free tone regions as spans with name chips pinned to the visible left edge, and
carries the editing grammar for boundaries: click selects, edge-drag moves a shared boundary
(snapped to the placement quantum), Alt enters the insert quasimode with a ghost boundary, Esc
cancels.
Boundaries and the split ghost render on the tempo grid's own integer pixel columns
(`gridAlignedX`; the ghost is a 1px column fill), so a preview sits exactly on the line it will
commit to.
Every gesture ends as **one intent** through its `Listener`
(`onToneBoundaryMoveRequested`, `onToneChangeInsertRequested`, ...) — the view never
mutates the model. Its input is the `makeToneTrackViewState` projection; the active region
highlight advances by sampling `ITransport` at vblank cadence.

*Design in flux: the active-vs-selected semantics of tone regions are proposed to change
(`docs/plans/in-progress/tone-active-vs-selected.md`, awaiting sign-off) — treat the selection
behavior described here as current, not final.*

## Automation lanes — `ToneAutomationLanesView`

One lane per automated parameter plus a trailing "+" lane, from the `makeToneAutomationViewState`
projection. The pointer/edit pipeline is **controller-centric** (2026-07-19), mirroring the tab
lane: the view forwards raw pointer events through a `ToneAutomationPointerEvent` (the sibling of
the tab lane's `ChartPointerEvent`) and paints the `insert_ghost` / `drag_preview` the controller
publishes back; the controller owns every hit-test, snap, placement, and drag-gesture decision, so
that policy is testable without JUCE. The view keeps only presentation — lane-resize, menus,
readouts, the typed-value callout, the tracking vblank. Each edit still commits as **one full
point-list intent on release**. The point gesture no longer needs a defer-mid-push guard: the
controller freezes it at press, so a mid-drag lane rebuild republishes the preview instead of
yanking the point from under the user (the view still defers state pushes during the presentational
lane-resize drag). Selection is identified by value (instance id, parameter id, exact grid
position), not by index, so it survives rebuild pushes.

**The chip column pins to the SELECTED TONE** (user ruling 2026-09-03). Every chip in this row —
the lane names and the trailing "+" alike — sits at the left of the tone the lanes belong to
(`pinnedChipLeft`, off the editable window, which IS the active region's span), scrolls with it, and
sticks at the window's left edge once that start has scrolled past: the tone regions' own label rule
one row up, and the ruler's pinned tempo and time-signature values before that. Clamping to the
canvas's left edge alone was the defect the origin gutter exposed — the canvas reaches left of time
zero now, so a chip pinned to nothing but the window floated out in pre-song space beside a tone
that starts later.

Sticking is **bounded by the thing being labelled**: once the window's left edge passes the tone's
END the column leaves with it rather than staying glued to the window over the dimmed, non-editable
area beyond. All three halves of that rule — pin, stick, slide off — are one function,
`stickyLabelLeft` (`rock-hero-editor/ui/src/timeline/sticky_label.h`, which also holds the ruler's
and the tab lane's separate `pinYieldsToIncomingLabel` succession law), which the tone regions' own
labels call too; the first hand-written copy of the rule in this row dropped the right bound, and
both the paint and the hit test then had a chip column that no press could ever act on. Absence
travels through the geometry helpers as an empty optional (`pinnedChipLeft`, `laneChipBounds`,
`plusChipBounds`), so the drawing and the hit test go dark together by construction.

**A mark pinned over the lane never shadows a target beneath it.** `hitAt` resolves one zone for
both the hover cursor and the press, and it resolves them in that priority: point handles first
(so a point at value 0 stays grabbable under the resize band), then the anchor's grab, then the
resize band, then lane area — and the lane's **name chip last**, even though the chip is painted
on top and pinned across the lane start where points and the anchor live. The chip is the lane
menu's only home, so it stays hittable, but it claims only pixels where a press would otherwise do
nothing but arm the caret: with Alt held even bare lane area outruns it, because Alt is the insert
quasimode. Alt therefore has to reach the hit test itself (`hitAt(point, alt_down)`) rather than
being read later by the mouse handler — that is what keeps the cursor honest about what the click
will hit. The same honesty is why the anchor's grab is its own zone rather than lane area the
controller happens to re-resolve: Alt does not reach the anchor, so a hover there must show the
handle cursor and no insert ring, which a zone shared with lane area could not say.

**Every lane begins at its anchor.** The anchor is a derived, read-only mark at the lane's start
carrying the parameter's pre-automation value — what the tone state says the knob is — sampled from
`IToneAutomation` per vblank so it follows a knob turn. It is never stored in `song.json`; the audio
write seam prepends it to the backend curve (see \ref guide_tracktion_adapter) and the lane draws it
as a **hollow** point rather than a solid one, because it is not an authored point: the center is
filled with the band the canvas paints beneath the lanes so the curve stops there, and it cannot be
selected or deleted, and the drawn curve ramps (or steps) from it into the first authored point
instead of flattening backwards. A lane with no authored points is therefore just its anchor's flat
line. What the anchor answers is a **drag, which authors a real point at the lane start** — wanting
a different start value is authored data, not a change to what the tone state says. The new point
lands on the curve, which at the lane start *is* the anchor's own value, and the drag pulls it from
there. Like a point handle, the press stays a click until the pointer crosses the framework's
click→drag threshold: a bare click on the anchor authors **nothing** (a point that only restates
the tone state's own value is still an edit the user did not ask for) and falls through to what a
plain lane-area click does at that pixel — seek and arm the caret. This is the one place the "Alt
authors" rule does not reach, because the anchor is a handle rather than empty lane area; Alt on
empty area still authors on the click itself, which is exactly the law the anchor's rule must not
disturb. It runs the same creation plan every other placement does, so its
occupied-slot and region-window refusals are shared. An authored point already sitting on the lane
start states the lane's value there, and no anchor mark is drawn for it. Evaluating that curve —
the drawn line, the on-curve landing value for a new point, the insert ghost's y, the caret square —
is one function, `toneAutomationCurveValueAtSeconds` in editor core, and it reports the *drawn*
value: the discrete snap belongs to creation, not to reading the curve, so the caret square can
never sit at a different height from the mark it is standing on.

**The anchor moves, so the backend curve is re-derived.** The audio seam bakes the anchor into the
Tracktion curve when it writes, while the lane re-reads it live every frame — two captures of one
mutable fact, which would drift apart the moment a knob turned. `rebuildDerivedToneCurves` closes
that: it re-derives every curve in the arrangement after a rig load, a tempo-map edit, and every
**settled plugin edit** — a knob gesture or an undone/redone plugin chunk, the only baseline moves
the editor can see honestly. (The gate that reports those edits rejects plugin-initiated value
changes, which is what keeps a plugin echoing an automated value back at us from being baked in as
a lane's start.) What is left is a knob still mid-gesture: the drawn anchor tracks it live and the
curve catches up when the gesture settles.

A scope note on editing: the interaction *grammar* (Ctrl precision, Alt create-quasimode, Shift
extend, snap always on, Esc cancel, one undo entry per gesture —
`docs/plans/in-progress/editing-interaction-model.md`) is settled and binding. It is implemented
on the tone track, the automation lanes, and — increasingly — the tab lane's chart editing (the
caret/marker model, note selection, typing-inserts, and Alt+click note create;
`docs/plans/roadmap/40-chart-editing.md` and
`docs/plans/in-progress/chart-span-and-selection-model.md`). Tempo-anchor editing is **not built
yet**. This guide gives the chart surfaces no detailed tour of their own; new editing surfaces
adopt the grammar as they arrive.

# Adding a new timeline row — silent steps

All of these compile clean when forgotten:

1. **View-state + projection** in editor core (`*_view_state.h`, `make*ViewState(...)` in a
   feature folder), derived in `deriveViewState()` and added to `EditorViewState`.
2. **The component**, following \ref guide_add_view (Listener, `setState`, theme).
3. **Viewport wiring** in `TrackViewport`: construct/parent the row in its canvas, stack it in
   the layout, and plumb `setVisibleTimeline` (from `pushCanvasTimeline`, never from
   `EditorView::setState` — see "one time window" above), `setGridLines` (if it draws the grid),
   `setVisibleContentLeft` (if it pins anything to the window's left edge — chips, labels, the
   tab lane's string legend), and height into the canvas layout. If the row
   can host an armed caret, publish its caret mask through the upward channel (see above) —
   the viewport must never poll it.
4. **`EditorView::setState` fan-out** — the row exists but renders defaults forever without it.
5. **Snapping through `musicalGridPositionForX`** for any gesture, and one-intent-on-release
   commit semantics.
6. **Tests**: projection tests in editor-core (headless), wiring tests via the UI harness.
