# Developer Guide Completion Plan

Status: in progress. The census below was taken 2026-07-15 against the current tree and the
15-page guide (`docs/developer/`). This plan tracks bringing the guide to full coverage of every
stable area, while explicitly *not* documenting unstable or unbuilt areas.

## Documentation policy for stability (binding for guide work)

- **Stable** — tour/document fully.
- **In flux** — light-touch only; every affected passage carries a one-line italic *Design in
  flux* note naming the owning plan file. Current flux areas: tone active-vs-selected
  (proposed), tone parameter automation (active), tone-track/tempo-map UI (active), app-local
  project view state (active), GameShell composition (decided, lands with plan 21 Phase 6).
- **Not built** — zero documentation beyond naming it as future work. Current: note authoring /
  chart editing / tempo-anchor editing (roadmap 40/41/42), game 2D tab view (roadmap 30),
  detection (roadmap 22/23), and everything later on the roadmap.

## Census summary (2026-07-15)

Covered by a dedicated tour or worked exemplar: engine + ports (transport, live rig, plugin,
clock, song, automation, input, settings/null), controller/action pipeline, undo, busy, signal
chain (all three layers), 2D timeline views (waveform/tab/tone/automation + viewport), 3D highway
(projection/renderer/both shells), musical time (types/TempoMap/mirror/playback clock/grid),
package format, tone designer (as a mode), game session/library/resources/settings, patterns
catalog, test-double taxonomy.

Gaps, ranked by size × centrality (from the full census):

| # | Area | Size | Today | Planned treatment |
|---|---|---|---|---|
| 1 | `common/audio/src/tracktion/` adapter internals | 24 files | mentioned | **New tour: "Inside the Tracktion adapter"** — rack assembly, plugin hygiene, monitoring rebuilds, thumbnail, tempo mirror. Stable core; keep automation-adjacent parts light (flux). |
| 2 | `editor/core/project/` lifecycle | 15 files | mentioned | **New tour: "The project lifecycle"** — workspace, project.json vs package, open/save/import (incl. GP import), dirty gating, deferred actions. Mostly stable; view-state persistence bits in flux (app-local plan). |
| 3 | `editor/{core,ui}/audio_device/` + `common/audio/device`+`settings` | 14 files | un/mentioned | **New tour: "Audio device settings and stores"** — nested MVC, per-app config stores (read-only game store), failure overlay. Stable. |
| 4 | `common/core/chart/` model | 3 files | mentioned | Fold a "chart model" section into \ref guide_musical_time or the package page; full tour **deferred until chart editing is built** (roadmap 40). |
| 5 | `common/core/shared/` + logging | 4 files | undocumented | Short section (likely in the patterns page or a small "infrastructure" page): logger facade + worker thread, JSON helpers, path bridging. Stable. |
| 6 | `game/core/{audio,input,menu,frame_clock,diagnostics}` | 6 files | un/mentioned | Extend \ref guide_game with a "supporting systems" section (route→slot mapping, bindings, frame clock already partly covered). Stable but small; GameShell flux note applies. |
| 7 | `game/ui/{dev,overlay}` dev tooling | 4 files | undocumented | One short "dev tooling" subsection in \ref guide_game (`--dev-package`, hot reload, frame-time overlay). Stable. |
| 8 | `editor/core/audio` config-store delegation | 2 files | undocumented | One paragraph in the audio-device tour (item 3). |
| 9 | `common/audio` `mix` + `tone_timeline` ports | 2 files | mentioned | Name them with one line each in the game tour (session ports); no dedicated pages. |
| 10 | `editor/ui/busy` + `input_calibration` windows | 4 files | mentioned | One paragraph each where their features are already toured. |

Items 1–3 are full pages; 4–10 are sections added to existing pages. Suggested order: 1, 2, 3,
then the section batch (4–10) as one change set.

## Verification per batch

Follow the established guide verification: rg-verify every cited symbol, 100-column check, docs
build with zero new warnings, mermaid render check in a browser for new diagrams, pre-commit.
Update the hub's routing table and deep-dive list with every new page.
