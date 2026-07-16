# Backlog

Small fixes and evaluations to do when there's time — short entries, not plans. Counterpart to
[watch-items.md](watch-items.md): backlog items you *do*, watch items you *monitor*. Anything
that needs a design or multiple steps belongs in a `docs/plans/todo/` plan instead. Delete an
entry when it's done — git history is the record.

- Tone changes should switch SLIGHTLY before the tone region begins (~100 ms ahead?) so the
  transition still feels seamless for players who are a little out of time — needs evaluation and
  tuning.
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
