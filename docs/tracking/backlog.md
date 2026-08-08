# Backlog

Small fixes and evaluations to do when there's time — short entries, not plans. Counterpart to
[watch-items.md](watch-items.md): backlog items you *do*, watch items you *monitor*. Anything
that needs a design or multiple steps belongs in a `docs/plans/todo/` plan instead. Delete an
entry when it's done — git history is the record.

- **Re-import EVERY package and project (2026-08-08, hard break).** The harmonic collapse replaced
  the `harmonic` and `touch` note fields with a single `harmonicNode`, and moved the pinch to
  `"attack": "pinch"`. The chart reader now **refuses** a document carrying either old key rather than
  ignoring it, so an un-reimported package fails to load with a message naming the fix — chosen over
  silent loading, which would have dropped every harmonic in the chart without a word. This affects
  the converter-sourced `.rock` corpus too, unlike the GP-only item below. The refusal in
  `chart_document.cpp` is a tripwire, not compatibility: delete it once the corpus is re-imported.

- Re-import GP-derived projects/packages saved before 2026-08-06: charts imported before the
  scoop/trail-off model (commits 6b5c9894/4d33abbf) still embed moved-head slide-ins and
  windowless trail-off FHP tracks (e.g. the Van Halen import project that motivated plan 55), and
  charts imported before the sustain-policy rules 2 and 3 were narrowed (2026-08-06) still embed
  tails that a non-changing payload held open past the margin — a bend plateau or a trailing
  equal-fret hold waypoint — plus sub-beat chords whose unbent strings lost their tails while a
  bent partner kept one, and charts imported before the hold-waypoint placement fix (2026-08-07)
  have a stray fret-hand position baked in at every equal-fret hold waypoint, which shifts the hand
  mid-note on a tie chain that holds a fret and then trails off. That last one persists in saved
  projects rather than being re-derived on load, because `fret_hand_positions` is serialized into
  the chart document — re-importing is the only way to clear it. GP-import normalization applies to
  GP import only, so the converter-sourced `.rock` corpus is NOT affected — only saves that passed
  through the GP importer.
- Lane-caret keyboard navigation does not glide the viewport the way chart-caret navigation
  reveals its measure (2026-07-18): stepping a lane caret can walk it off-screen. Extend the
  caret-measure reveal to the lane caret's published seconds.
- Game F1 diagnostics overlay: the per-frame values line updates so fast it is unreadable while
  playing (witnessed during the 2026-07-16 milestone-0 soak — clock drift could not be read).
  Throttle the readout refresh (~2-4 Hz) and/or use fixed-width numerals so values stop
  jumping.
- Tone changes should switch SLIGHTLY before the tone region begins (~100 ms ahead?) so the
  transition still feels seamless for players who are a little out of time — needs evaluation and
  tuning.
- `g_capital_ink_fraction` (0.55, `tab_paint_core.cpp`) understates a capital's real ink by ~8.8%,
  so the T/S/P plates draw their letter at note heights where it spills ~0.19 px past the plate's
  inner height, and every slot mark's `tuck` floor is that much too small. Measured 2026-08-06:
  Verdana Bold's cap height is 1489/2048 em against a JUCE font height (ascent+descent) of
  1.215332 em, i.e. 0.5983 — 7.484 px measured at font height 12.5. Currently UNREACHABLE in the
  editor, because `TrackViewport` fixes the canvas so `note_height` clamps to 25 at every string
  count; it bites the game's 2D strips (plan 30) or any resizable row. Do NOT just write 0.5983:
  Verdana is only JUCE's Windows default sans
  (`juce_DirectWriteTypeface_windows.cpp:417`; Linux resolves Sans/Arial/Ubuntu), so a hardcoded
  ratio bakes in a platform-specific font fact. `GlyphArrangement` cannot measure it either — a
  `PositionedGlyph`'s bounds are the layout box, full font height rather than tight ink
  (`juce_GlyphArrangement.h:75`). Needs a per-typeface measurement or a deliberately conservative
  bound.
- Evaluate VST2 support feasibility.
- Automation lane "+" should look closer to the signal-chain "+" for visual consistency.
- Report the bgfx Conan-package issue upstream to conan-center — we rolled our own recipe because
  of a dependency clash with SDL3, but never filed the issue.
- Evaluate error-header organization: whether each error type gets its own header, or each domain
  gets a single domain-level errors header, to keep error classification organized project-wide.
- Generate a modern-C++-expert agent aware of the latest C++ features — it should know the actual
  current standard but give advice for the C++ version the project actually uses.
- Waveform drawing doesn't always finish before the project finishes loading — evaluate (may be
  fine to keep as-is).
- Playback should continue to the end of the grid even when the audio ends early (just silence
  after that point).
- Audit the project for position types that duplicate logic. (ToneGridPosition — the specific
  case that prompted this — is already removed from the code; only stale doc mentions remain.)
- Consider a plan for a basic suite of drop-in "standard tones" built only from plugins we ship
  with the project (so every user has them). Note (2026-07-11, supersedes the old "mandatory
  fallback" clause): 21-Q1 settled missing-plugin handling as refuse-to-start; standard tones
  would back the PINNED opt-in "play with default tones" option on that refusal (watch item in
  watch-items.md), never an automatic substitution.
- Editor: warn on export/publish to `.rock` when any tone's summed reported plugin latency is
  high (~10 ms+), so a charter cannot ship an unintentionally high-latency tone (21-Q2
  refinement: the game stays silent; the guard lives at authoring time). Data source: plan 21
  Phase 5's per-tone latency surfacing on the rig-load result. DEFERRED follow-on, recorded so
  it is not lost: a save-file flag marking high-latency tones so players could be alerted — a
  format change routed through plan 10 if ever adopted.
- Inlay UV half-texel bleed — the fretboard-skin inlay quads skip the half-texel inset the atlas
  layout applies to note/glyph cells, so linear filtering can bleed a sliver of the neighboring
  cell at a marker edge (cosmetic).
- Chord-box filling no longer tints its own chord's heads — accepted cost of drawing boxes before
  the notes (our board view has no depth writes); revisit only if it reads wrong in practice.
- Eyeball the un-witnessed Phase 4 technique renders — hammer-on/pull-off/tap icons, harmonic
  heads, arpeggio brackets, tremolo wobble — on a legato/harmonic-heavy chart.
- Evaluate the engine ctor's unconditional auto-detect `initialise(1, 2)` followed by the
  editor's saved-route restore (a double device open at startup) — needs its own design and
  touches game startup too.
- Evaluate `handleAudioDeviceConfigurationRefresh` ordering: it detaches instrument monitoring
  before `dispatchPendingUpdates()` rebuilds Tracktion's wave list, churning playback contexts
  against a stale device list (crash hypothesis H3 from the 2026-07-14 reconnect investigation;
  much tamer now that the policy no longer reopens devices mid-pass).
- Evaluate gating `audioMeterSnapshot()` on `m_audio_device_configuration_refresh_pending` the
  way `currentInputDeviceIdentity()` already is (H4 from the same investigation; largely
  mitigated by the stable structural meter plugins).
- Decide UX for the audio-device failure popup vs. the input-calibration prompt appearing
  together (device disconnect while the calibration prompt is up) — modals currently just stack.
- Move section names in the 3D view to the upper-left corner instead of inline with the
  scrolling tab.
- Evaluate per-(project, arrangement) resume-marker records: the marker settings family is
  per-project today, so switching arrangements loses your spot in the previous one. Additive
  key-shape change in `EditorSettings` whenever it itches in practice.
