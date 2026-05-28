# Public Header Surface Review

Status: completed review. The immediate editor-core concrete-header cleanup has been applied.
Remaining watch items in this note are deferred guidance to re-evaluate before related work.

## Scope

This note reviews the public header surface of each project-owned library and asks whether the
headers under `include/rock_hero/...` are real cross-target contracts or implementation details
that could move closer to their owning `src/` folder.

The main goal is not to minimize public headers for its own sake. The goal is to keep exported
interfaces intentional, make class roles clearer, and avoid making implementation helpers look like
stable architectural contracts.

## Summary

Most public headers are justified. The current include layout already keeps detailed editor UI
widgets private under `rock-hero-editor/ui/src`, and the shared domain, audio port, editor workflow,
and view-state headers generally represent real module boundaries.

There are a few headers that look more public than they need to be:

- `rock-hero-editor/core/include/rock_hero/editor/core/inline_editor_task_runner.h`
- `rock-hero-editor/core/include/rock_hero/editor/core/audio_device_status_text.h`
- `rock-hero-editor/core/include/rock_hero/editor/core/rock_song_importer.h`
- `rock-hero-editor/core/include/rock_hero/editor/core/psarc_song_importer.h`
- `rock-hero-editor/ui/include/rock_hero/editor/ui/main_window.h`

These are the strongest candidates for tightening because they are concrete implementation choices,
UI shell composition, or display policy rather than broadly useful public contracts.

## Library Findings

### `rock-hero-common/core`

The public headers in `common/core` should remain public. They define shared song, arrangement,
package, archive, time, and utility types used across products and tests. These are durable domain
and package contracts rather than private implementation helpers.

No move is recommended.

### `rock-hero-common/audio`

Most public headers in `common/audio` should remain public. The module intentionally exports audio
ports, value types, error types, and the concrete `Engine` composition type while keeping Tracktion
implementation details private.

Watch items:

- `i_edit.h` is currently more of a reserved contract than an active one. Keep it if the project
  still wants a visible future edit-command boundary. Otherwise, remove it until real behavior
  exists.
- `scoped_listener.h` is a generic helper. It is public today because public-facing controller
  state uses it directly. Moving it private would likely require a PIMPL or different ownership
  shape, so it is not a low-risk header move.

No immediate move is recommended.

### `rock-hero-common/ui`

There are no meaningful public UI headers to move right now.

No action is recommended.

### `rock-hero-editor/core`

Most public headers are justified because they define editor workflow, session state, controller
interfaces, settings, view-state projections, and song-import contracts consumed by UI, app code, or
tests.

Move candidates:

- `inline_editor_task_runner.h`: this is an internal fallback implementation for controller task
  dispatch. The public contract is `IEditorTaskRunner`; the inline implementation can likely move
  to `src/` unless tests or app code need to construct it directly.
- `audio_device_status_text.h`: this is display-projection policy. Consider keeping only durable
  state constants public, if needed, and moving formatter behavior private to the controller or a
  `src/` helper.
- `rock_song_importer.h` and `psarc_song_importer.h`: these are concrete default importers. The
  public contract is `ISongImporter`. Move the concrete importer headers private unless app code is
  expected to choose importer implementations explicitly.

Recommended direction: keep public interfaces and state models public, but move concrete fallback
and default implementation headers private when they are constructed only inside editor core.

### `rock-hero-editor/audio`

There are no meaningful public editor audio headers to move right now.

No action is recommended.

### `rock-hero-editor/ui`

The detailed editor UI components are already private under `src/`, which is the right direction.
The public UI surface is small.

Watch item:

- `editor.h` should remain public. It is the ready-to-host editor feature boundary.
- `main_window.h` is more questionable. If `MainWindow` is only the executable shell that hosts the
  editor component, it belongs in `rock-hero-editor/app/` rather than the reusable UI library. If it
  is intended to be a reusable product-level window abstraction, keeping it public is acceptable but
  should be treated as an intentional API decision.

Recommended direction: consider moving `MainWindow` into the app target during a focused app-shell
cleanup. Do not mix that move into unrelated UI layout work.

### `rock-hero-game`

The game libraries currently have no meaningful public header surface requiring cleanup.

No action is recommended.

## Recommended Order

1. Move `InlineEditorTaskRunner` private if only `EditorController` constructs it.
2. Split `AudioDeviceStatusText` into public state, if needed, and private formatting policy.
3. Decide whether concrete song importers are app-selected implementations or editor-core defaults.
4. If they are editor-core defaults, move `RockSongImporter` and `PsarcSongImporter` private.
5. Decide whether `MainWindow` is reusable UI or app-shell composition.
6. Defer `ScopedListener` cleanup unless a broader controller or listener ownership refactor makes
   it easy.
7. Revisit `IEdit` when edit-command behavior becomes real enough to justify the boundary.

## Non-Goals

- Do not move headers only because they currently have few consumers.
- Do not hide public ports, state models, or error types that define real module boundaries.
- Do not add wrapper types only to make a header private.
- Do not update durable `docs/design/` policy from this review alone.
- Do not disturb placeholder libraries that have no meaningful public API yet.

## Practical Bar For Moving A Header

A header is a good move candidate when all of these are true:

- it defines a concrete implementation, not the project-owned contract;
- only the owning library constructs or depends on the type;
- tests can still cover the behavior through the public contract or a same-library test target;
- moving it does not force a new abstraction, PIMPL, or friend-only test surface.

If moving a header requires new machinery that does not simplify the project, keep it public for
now and revisit it when the surrounding ownership model changes.
