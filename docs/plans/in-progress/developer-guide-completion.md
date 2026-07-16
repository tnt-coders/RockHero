# Developer Guide Completion Plan

Status: census complete, all items executed 2026-07-15 (the guide is 18 pages). The plan stays
open as the standing coverage registry: it tracks keeping the guide complete over every *stable*
area while explicitly *not* documenting unstable or unbuilt ones.

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
| 1 | `common/audio/src/tracktion/` adapter internals | 24 files | **DONE 2026-07-15** | `docs/developer/inside-the-tracktion-adapter.md` — rack assembly, plugin hygiene, timing/automation bridges, device glue, the pure-policy split pattern. Automation parts carry the flux note. |
| 2 | `editor/core/project/` lifecycle | 15 files | **DONE 2026-07-15** | `docs/developer/the-project-lifecycle.md` — workspace model, .rhp vs .rock, open-flow diagram, deferred-action gate, save-is-publish, importers, startup restore, dirty/fault interactions. View-state flux noted. |
| 3 | `editor/{core,ui}/audio_device/` + `common/audio/device`+`settings` | 14 files | **DONE 2026-07-15** | `docs/developer/audio-device-settings.md` — staged-settings transaction, the ephemeral sub-MVC + dispatcher, read-only game store, native setup machine. Also covers item 8 (config-store delegation). |
| 4 | `common/core/chart/` model | 3 files | **DONE 2026-07-15** | Brief section in `musical-time.md`; full tour deferred until chart editing is built (roadmap 40). |
| 5 | `common/core/shared/` + logging | 4 files | **DONE 2026-07-15** | Logging-facade entry added to `cross-cutting-invariants.md`. |
| 6 | `game/core/{audio,input,menu,frame_clock,diagnostics}` | 6 files | **DONE 2026-07-15** | "Supporting systems" section added to `game-development.md`; game/core/audio covered in `audio-device-settings.md`. |
| 7 | `game/ui/{dev,overlay}` dev tooling | 4 files | **DONE 2026-07-15** | "Dev tooling" section added to `game-development.md`. |
| 8 | `editor/core/audio` config-store delegation | 2 files | **DONE 2026-07-15** | Covered in `audio-device-settings.md` (the read-only game store section). |
| 9 | `common/audio` `mix` + `tone_timeline` ports | 2 files | **DONE 2026-07-15** | Named with roles in `game-development.md` supporting systems. |
| 10 | `editor/ui/busy` + `input_calibration` windows | 4 files | **DONE 2026-07-15** | Busy overlay named in invariants; calibration window covered in `audio-device-settings.md`. |

All census items are DONE as of 2026-07-15. This plan stays open as the registry for future
coverage: new subsystems get a census row (with a stability flag) when they land, and the
chart-model full tour activates when chart editing (roadmap 40) is built.

## Verification per batch

Follow the established guide verification: rg-verify every cited symbol, 100-column check, docs
build with zero new warnings, mermaid render check in a browser for new diagrams, pre-commit.
Update the hub's routing table and deep-dive list with every new page.
