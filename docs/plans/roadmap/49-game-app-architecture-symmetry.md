# Plan 49 — Game app architecture symmetric with the editor

**Status:** Complete 2026-07-12. Phase 1 (editor rename) @ 47947fc8; Phases 2-4 (`Game` +
`SDL3Application` + `RockHeroGame`, `GameShell` deleted) @ 4d61b97b. Behavior-preserving; build
-Targets all + all 7 test suites green. Public headers placed under `include/.../ui/surface/`
mirroring `src/surface/` per the module convention.

**One-line:** Split the hollow `GameShell` into the same clean layering the editor already has —
a framework/loop base, a composition-root app object, a window object, and a content object — and
rename the editor's app class to match the new naming rule.

## Motivation

The editor has a clean four-layer composition:

`juce::JUCEApplication` (framework: owns the run loop + lifecycle) → `RockHeroEditorApplication`
(composition root: owns engine/settings/monitor, constructs the window) → `MainWindow` (owns the
OS window, hosts content) → `Editor` (the content/feature object).

The game has none of that structure. `GameShell` is a **hollow class with zero data members**
whose single `run()` method (`rock-hero-game/ui/src/surface/game_shell.cpp:278-` , ~200 lines)
constructs *everything* as locals and fuses three roles the editor keeps separate:

- the **window/loop host** (the `MainWindow` role),
- the **content** (the `Editor` role — highway renderer, menu, diagnostics, dev-session),
- the **frame-loop driver** (a role JUCE owns for the editor).

And there is no game equivalent of `RockHeroEditorApplication` at all — composition is smeared
across `main()` locals, the fat `GameShellOptions` bag, and `run()` locals. This is the root of the
"GameShell feels smelly" observation: it is a free-function-in-a-class carrying a composition that
wants to live in real objects.

## Current-state inventory

Verified against code on 2026-07-12, refactor @ 005a0e5a.

`GameShell::run()` (`game_shell.cpp:278-`) constructs as locals, in order:

| Local | Role it belongs to |
|---|---|
| `GameWindow::create(...)` (`:280`) | **window** — the true `MainWindow` analog; already its own type |
| `common::ui::RenderDevice::create(...)` (`:291`) | render surface (bgfx device) |
| `GameResources` + `HighwayShaderSet` + `HighwayTextureSet` (`:309-350`) | resources |
| `HighwayRenderer::create(...)` (`:351`) | **content** |
| `DiagnosticsOverlay` + `DiagnosticsController` (`:358-362`) | **content** |
| `DevSession` (`:367`) | **content** (chart projection) |
| `GameplaySession*` (`:389`, injected via options) | injected dependency (keep in `main()`) |
| `SongSelectMenu` + `MenuBindings` + `in_menu` (`:419-437`) | **content** (menu) |
| `launchSong` lambda (`:442`) | **content** (intent execution) |
| `FrameClock` / `FramePacingStats` / boundary / `frames_submitted` (`:475-480`) | **loop/pacing** |
| `while (true) { ... }` (`:481`) | **loop** |

`main()` (`rock-hero-game/app/main.cpp:216-280`) owns the JUCE runtime guard, engine, gameplay
session, and scanned library, and injects them through `GameShellOptions` into
`GameShell{}.run(options)`. `GameWindow` (`game_window.h`) already owns the SDL window + event
polling — it is the real `MainWindow` analog, not `GameShell`.

## Target architecture

| Editor | Game (target) | role |
|---|---|---|
| `juce::JUCEApplication` (vendor) | **`SDL3Application`** (ours) | framework host: run loop + per-frame JUCE-message drain + vsync pacing |
| `RockHeroEditor` *(rename of `RockHeroEditorApplication`)* | **`RockHeroGame`** | composition root: owns window/device/resources/scene + injected session/library |
| `MainWindow` | `GameWindow` *(exists)* | owns the OS window + event polling |
| `Editor` | **`Game`** (new) | content: highway renderer, menu, diagnostics, dev-session, `launchSong`, per-frame update/render |

Naming rule this establishes: **framework bases keep "Application" (`JUCEApplication`,
`SDL3Application`); our concrete app objects drop it (`RockHeroEditor`, `RockHeroGame`).**

Ownership mirrors the editor exactly: the base owns only the loop cadence; the **app owns the
window** (as `RockHeroEditorApplication` owns `MainWindow`), the device, the resources, and the
scene; **composition (engine/session/library) stays in `main()` and is injected** (as the editor
composes in the app and injects into `Editor`). `GameShell` is deleted.

## Decisions (signed off 2026-07-12)

- **49-D1 — `SDL3Application` is a base class: YES.** It owns the loop + per-frame JUCE-drain + vsync
  pacing (thin, mirroring `JUCEApplication`). Accepted despite having a single subclass because the
  SDL/bgfx/JUCE-pump plumbing is real, cleanly separable, and makes the editor parallel exact. If it
  ever proves too thin, collapsing it into `RockHeroGame` is the fallback.
- **49-D2 — Base name: `SDL3Application`** (SDL owns the loop; bgfx + the JUCE pump ride inside it).
- **49-D3 — `RockHeroGame : SDL3Application` (inherit),** matching
  `RockHeroEditorApplication : juce::JUCEApplication`. `SDL3Application::run()` is a template method
  calling virtual `onInit()` / `onFrame()` / `onShutdown()` that `RockHeroGame` overrides.
- **Content object name: `Game`** (`game::ui::Game`), matching `editor::ui::Editor` — the app is the
  `RockHero`-prefixed shell (`RockHeroGame`), the content is the bare feature name. The mild
  redundancy of `game::ui::Game` is the same one `editor::ui::Editor` already carries; symmetry is
  the priority, so `Editor` is NOT renamed.

## Phases

1. **Editor rename** — `RockHeroEditorApplication` → `RockHeroEditor` (`rock-hero-editor/app/main.cpp`,
   the `START_JUCE_APPLICATION` arg, its own references). Isolated, mechanical; note the proximity
   to `editor::ui::Editor` (different namespace/TU — acceptable). Independent of the game work.
2. **Extract `Game`** — a content object (game/ui) owning the highway renderer, diagnostics,
   dev-session, menu + bindings + `in_menu`, and `launchSong`, exposing `handleInput(events)` /
   `update(song_time)` / `render(device)`. `GameShell::run()` drives it; `run()` shrinks to
   window + device + resources + loop + a `Game`. No behavior change.
3. **Introduce `RockHeroGame`** — the composition root (game/app or game/ui) owning `GameWindow`,
   `RenderDevice`, `GameResources`, and `Game`, holding the injected `GameplaySession*` +
   `LibraryIndex`. `main()` constructs `RockHeroGame` and runs it; the `GameShellOptions` bag
   collapses into `RockHeroGame`'s constructor + members. Composition stays in `main()`.
4. **Introduce `SDL3Application`** (if 49-D1 = yes) — extract the loop + JUCE-drain + vsync pacing
   into the base; `RockHeroGame : SDL3Application` overrides the per-frame hook. **Delete
   `GameShell`.**

## Constraints

- Naming firewall (never the commercial real-guitar game's name); `color`-spelling convention; no
  namespace aliases; no NOLINT; `std::expected` for recoverable errors.
- Layering: composition/adapter construction stays in `app/`; game/core stays SDL-free (the SDL
  keycode bindings stay at the `game/ui` composition boundary, as today at `game_shell.cpp:418`).
- Build only through `.agents/rockhero-build.ps1`; keep the smoke-run `frame_limit` hook working;
  the dev-diagnostics layer and dev-package/library paths must behave identically.
- Behavior-preserving throughout: the rendered output, menu flow, and audio session wiring are
  byte-for-byte unchanged; this is a structural refactor only.

## Relationship to other plans

Sequences naturally with the game track (plans 20/21/26/27). Not on the audio critical path.
Phase 1 (editor rename) can land anytime. The game phases are best done before plan 26 Phases 5-7
grow the menu/startup surface further, so that new game UI is written against `Game` rather
than more `run()` locals.
