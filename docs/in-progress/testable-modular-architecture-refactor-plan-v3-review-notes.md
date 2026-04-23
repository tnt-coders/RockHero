# Review Notes for Revised v3 Testable Architecture Plan

## Summary

`testable-modular-architecture-refactor-plan-revised-v3.md` is the strongest version so far.
It addresses the main concerns from the previous review:

- `juce::File` is removed from presenter and port contracts.
- `TransportState` lives in the audio API while core owns reusable time value types.
- The dependency diagram uses explicit arrows.
- The presenter test no longer assumes optimistic UI updates.
- The `architecture.md` dependency update happens with the first `audio -> core` change.
- `audio::Thumbnail` is no longer treated as equivalent to a fakeable business-logic port.

The port-vs-translation-adapter distinction is a useful correction. It reflects the current
codebase better than treating every abstract audio-facing type as the same kind of seam.

The concerns below should be patched before using v3 as the final implementation guide.

## 1. Thumbnail Ownership in the Composition Sketch Is Unsafe

The v3 composition sketch creates the view, then creates the thumbnail, then passes a raw thumbnail
pointer into the view:

```cpp
auto engine    = std::make_unique<rock_hero::audio::Engine>(/* ... */);
auto view      = std::make_unique<rock_hero::ui::EditorView>();
auto thumbnail = engine->createThumbnail(view->waveformComponent());
view->attachThumbnail(thumbnail.get());

auto presenter = std::make_unique<rock_hero::ui::EditorPresenter>(*engine);
presenter->attachSink(*view);

view->setPresenter(*presenter);
```

This has a lifetime problem. Local variables are destroyed in reverse construction order:

1. `presenter`
2. `thumbnail`
3. `view`
4. `engine`

If `view` stores `thumbnail.get()`, then `view` can outlive the thumbnail pointer it stores.

The plan correctly states that thumbnails must not outlive the `audio::Engine` that created them,
but it also needs a rule for thumbnail vs. view/component lifetime.

Recommended revision:

- Make `EditorView` or `WaveformDisplay` own the `std::unique_ptr<audio::Thumbnail>`.
- The app composition root creates the thumbnail through `Engine`, then transfers ownership into
  the view.
- The view must be destroyed before the engine, which already matches the current
  `MainWindow` member order if the engine is declared before the content/view.

Example:

```cpp
auto engine = std::make_unique<rock_hero::audio::Engine>(/* ... */);
auto view = std::make_unique<rock_hero::ui::EditorView>();

view->attachThumbnail(engine->createThumbnail(view->waveformComponent()));

auto presenter = std::make_unique<rock_hero::ui::EditorPresenter>(*engine);
presenter->attachSink(*view);
view->setPresenter(*presenter);
```

Where the view API takes ownership:

```cpp
void EditorView::attachThumbnail(std::unique_ptr<audio::Thumbnail> thumbnail);
```

Document the final lifetime rule:

- `EditorView`/`WaveformDisplay` owns and destroys the thumbnail.
- `audio::Engine` must outlive every thumbnail it created.
- The app/window composition root must destroy the view before the engine.

## 2. `audio::Thumbnail` Is Not a Primary Test Seam, But "Not Fakeable" Is Too Absolute

V3 says `audio::Thumbnail` has "no fake, no substitute, and no unit-testing story."

The architectural point is right: `Thumbnail` is not like `ITransport`. It is not a
framework-free business-logic port, and presenter tests should not depend on it.

However, the wording is too absolute. `audio::Thumbnail` is an abstract class, so a limited fake
could be written for narrow JUCE component tests. For example, a fake thumbnail could return a
duration and record `drawChannels()` calls.

Recommended wording:

- `audio::Thumbnail` is not intended as a primary unit-test seam.
- It is not suitable for framework-free presenter tests because its contract includes JUCE drawing
  and component repaint behavior.
- It may still be faked selectively for narrow JUCE component wiring/layout tests if useful.

This preserves the port-vs-adapter distinction without overstating what the type can or cannot do.

## 3. Thumbnail Creation Must Stay in the App Composition Root

V3 says `rock-hero-ui` may not depend on `audio::Engine`, while `Engine::createThumbnail(...)`
remains the only thumbnail factory.

That is coherent only if thumbnail creation happens outside UI, in the app/window composition root.
The plan's direction implies this, but it should say it explicitly.

Recommended revision:

- `EditorView` and `WaveformDisplay` receive an already-created `audio::Thumbnail`.
- UI code must not call `audio::Engine::createThumbnail(...)`.
- The app composition root is responsible for calling `Engine::createThumbnail(...)` and
  transferring thumbnail ownership into the view.

This prevents the UI from drifting back toward `#include <rock_hero/audio/engine.h>`.

## 4. `Thumbnail::setSource(core::AudioAsset)` Is Another `audio -> core` Dependency

V3 updates `Thumbnail::setFile(const juce::File&)` to:

```cpp
Thumbnail::setSource(const core::AudioAsset& asset)
```

This is consistent with the new dependency rule. It is not a blocker.

Still, the plan should call out that thumbnail source assignment is another reason
`rock-hero-audio` depends on `rock-hero-core`, not only `ITransport`.

Recommended revision:

- In refactor step 3, explicitly state that `Thumbnail::setSource(core::AudioAsset)` uses the same
  `audio -> core` dependency introduced in step 1.
- Keep the `architecture.md` dependency-line correction in step 1.

## Recommendation

Use v3 as the base, but patch the thumbnail ownership and composition-root wording before treating
it as final.

The only materially risky issue is ownership: a raw thumbnail pointer held by the view can become
dangling in the current sketch. The other concerns are wording and precision improvements.

Once ownership is corrected, v3 is coherent enough to drive implementation.
