# v15 Concerns and Questions

Source plan:
`docs/in-progress/testable-modular-architecture-refactor-plan-revised-v14.md`

This document tracks concerns, questions, and possible v15 follow-up changes that come up during
the walkthrough of the testable modular architecture refactor plan.

## Open Concerns

- None currently.

## Resolved or Clarified

- `core::PlaybackPosition` and `core::PlaybackDuration` should remain separate lightweight
  value types even though both currently wrap `double seconds`. Rationale: position is a
  point on the transport timeline, duration is a span, and separate types prevent API
  mixups such as passing a track length where a seek position is expected. Keep both
  types intentionally boring for now: a `seconds` field plus equality, without a units
  framework or clever operator overloads.
- The view-output interface should likely be named `IEditorView` rather than
  `IEditorViewSink`. The "sink" terminology is technically accurate but reads awkwardly
  for this codebase. `IEditorView`, `FakeEditorView`, and `attachView(...)` make the
  production and test code read more naturally while preserving the important design
  constraint: the interface stays intentionally narrow and only exposes
  `setState(const EditorViewState&)`.
- Do not add a presenter state setter for testing. Tests should observe the presenter's
  real output path by attaching a fake implementation of the view interface. This tests
  the production behavior `transport event -> presenter derives state -> view receives
  state`, including initial state flushes, duplicate-push suppression, and null-view
  tolerance.
- Asset identity should not be treated as part of transport state conceptually,
  regardless of copy cost. Assets change on load/session changes, not on transport ticks.
  v15 should separate high-frequency transport state from lower-frequency loaded-audio
  or session-content state.
- Introduce a higher-level pure C++ `Session` model that owns transport/session state
  plus a simple ordered track collection. Transport state should contain only genuine
  play/pause/position/length-style data, while tracks contain audio asset information
  for full-mix or stem playback.
- JUCE has built-in undo/redo support and may be useful later, but putting JUCE directly
  into `core::Session` would violate the framework-free `rock-hero-core` boundary. The
  first design should keep the session model and editor commands pure C++, while a JUCE
  adapter or application-layer coordinator can use `juce::UndoManager` later if that
  proves useful.
- Tracks should start without fixed semantic roles such as `FullMix`, `Drums`,
  `Bass`, or `Vocals`. A simple ordered collection of tracks is enough for the first
  design, and role metadata can be added later if real editor or playback behavior needs
  it.
- Prefer `EditorController` over `EditorPresenter` for the class that receives editor
  user intents, coordinates session/transport behavior, and derives `EditorViewState`.
  In this codebase, "controller" reads more directly because the class handles user
  interaction methods such as play/pause, stop, waveform clicks, and load requests.
- Because the editor should eventually display waveforms for every loaded stem/track,
  the view state should not contain a single `loaded_asset` or `waveform_asset` as the
  primary content model. It should derive a collection of track/waveform row states
  from `core::Session` so the current one-file editor is just the one-track case.
- `EditorState` is shorter than `EditorViewState`, but it risks sounding like the
  actual editor/session/domain state. Keep evaluating the name in v15; if shortened,
  document clearly that it is a UI rendering snapshot, not `core::Session` and not the
  undoable document model.
- `TransportControls` should not expose public callback member variables in the plan
  sketches. Project clang-tidy rules reject public data members, and a listener or
  private callback with setter would better match existing C++ style. Prefer a narrow
  listener interface if consistency with the rest of the plan matters more than local
  callback convenience.
- `TransportControls` should use a local nested `Listener` interface with a required
  reference passed at construction. This is consistent with the plan's reference-based
  required dependencies and avoids half-wired child controls.
- `EditorView` should depend on a narrow intent interface, likely `IEditorController&`,
  rather than directly requiring a concrete `EditorController&`. This keeps the view
  fully wired at construction while preserving easy view-wiring tests with a fake
  controller.
- Do not add `ITransportControls` yet. `TransportControls` is an internal child widget
  owned by `EditorView`, not an architectural boundary. The controller's test seam is
  the whole `IEditorView`; abstract child widgets only if a concrete testing or reuse
  pressure appears later.
- Avoid exposing half-wired `EditorView` / `EditorController` objects to the rest of
  the app. If late binding is needed because the view and controller need references
  to each other, hide that binding inside a small composition object or factory that
  returns a fully wired editor feature/screen. Tests can use the same assembly shape
  with fake session/audio/view pieces where needed.
- Name the composition wrapper `ui::Editor`. Its role is the fully wired editor UI
  feature: it owns `EditorController` and `EditorView`, performs the internal
  `attachView` binding, and exposes the root JUCE component. Keep the domain model
  named `core::Session` so `Editor` does not become ambiguous with the document state.
- Keep `EditorController`, `EditorViewState`, `IEditorView`, and `IEditorController`
  inside the `rock-hero-ui` module as framework-free presentation submodule code. This
  matches the earlier plan shape: `rock-hero-ui` contains both JUCE widgets/views and
  non-JUCE presentation/controller logic. A separate CMake target remains a future
  promotion option if a documented tripwire fires, but v15 should not introduce that
  split by default.
- A standalone thumbnail factory may be excessive for v15 because `audio::Engine`
  already owns the Tracktion context needed to create `TracktionThumbnail` instances.
  Prefer keeping thumbnail creation on the audio engine/composition boundary for now,
  while shaping the UI state as track rows so moving to multiple thumbnails later is
  local to app/view composition.
- When a track's `AudioAsset` changes, the corresponding waveform row should
  automatically point its thumbnail at the new asset as part of applying
  `EditorViewState`. The normal editor flow should not require a separate manual
  "refresh thumbnail" call after every successful track asset update.

## Candidate v15 Changes

- Add a short rationale near the `PlaybackPosition` / `PlaybackDuration` definitions:
  the types are deliberately lightweight semantic wrappers used to make timing APIs
  harder to misuse as rhythm-game timing, calibration, hit windows, and seek math grow.
- Rename `IEditorViewSink` to `IEditorView`, `FakeEditorViewSink` to `FakeEditorView`,
  and `attachSink(...)` to `attachView(...)` in v15. Add a note that `IEditorView` is a
  deliberately narrow presenter-facing interface, not the full JUCE component surface.
- Rename `EditorPresenter` to `EditorController` in v15 while preserving the same testable
  role: no JUCE dependency, receives view intents, coordinates session/transport ports,
  derives `EditorViewState`, and pushes state through `IEditorView`.
- Replace the single-asset `ITransport` shape with a split design: transport controls
  and transport state stay focused on play/pause/stop/seek/position, while loaded audio
  asset identity moves into `core::Session` and its track collection. The current
  single-audio-file editor flow becomes a one-track session rather than a special
  transport-owned asset.
- Introduce the session model without mandatory track roles. Prefer `Session` owning a
  simple track collection initially, with each `Track` carrying identity, display name,
  audio asset reference, and basic playback flags only if needed by the current plan.
- Keep the first undo/redo design framework-neutral. Mention that JUCE undo/redo can be
  integrated later at an adapter/application layer, but avoid making `core::Session`
  depend on JUCE in v15.
- Replace single-asset `EditorViewState.loaded_asset` with a vector of track/waveform
  row states, even if v15 only populates one row at first. This keeps the view contract
  aligned with future stem display without requiring stem-specific roles now.
- Replace the `TransportControls` public `std::function` callback sketch with either
  a nested listener interface or private callbacks installed through setters. A listener
  interface is likely more consistent with `ITransport::Listener` and avoids public
  member data.
- Specify `TransportControls::Listener&` construction instead of nullable listener
  pointers or public callbacks.
- Add an input-side editor interface, tentatively `IEditorController`, implemented by
  `EditorController` and consumed by `EditorView`. Keep the output-side interface as
  `IEditorView`, implemented by `EditorView` and consumed by `EditorController`.
- Keep `TransportControls` concrete inside `EditorView`; do not introduce
  `ITransportControls` in v15.
- Add a `ui::Editor` composition wrapper that owns/wires `EditorController` and
  `EditorView` in one constructor. The wrapper may perform any necessary internal
  attach calls, but callers only receive/use a fully wired editor root component.
- Keep the controller/state/interface files under `libs/rock-hero-ui` in a conceptual
  framework-free `controllers/` submodule folder. Preserve the plan's CMake rule:
  keep `rock_hero_core`, `rock_hero_audio`, and `rock_hero_ui` until a promotion
  tripwire justifies splitting a submodule into its own target.
- Do not introduce a separate `IThumbnailFactory` in v15 unless the implementation
  needs to create/remove thumbnail rows dynamically inside `EditorView`. Prefer app
  composition creating the needed thumbnail adapters through `Engine::createThumbnail`
  and passing/attaching them to the view. Revisit a factory or thumbnail cache when
  multi-track row creation becomes dynamic enough to justify it.
- Specify that `EditorView::setState(...)` or the waveform row component diffs each
  track row's `AudioAsset`; when it changes, that row calls
  `audio::Thumbnail::setSource(asset)` so the new thumbnail generation starts
  automatically.
