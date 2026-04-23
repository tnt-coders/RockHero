# Testable Modular Architecture Refactor Plan v20 - Implementation Index

## Purpose

This v20 series breaks
`testable-modular-architecture-refactor-plan-revised-v19.md` into commit-sized
implementation stages.

One v19 decision is intentionally superseded here: the audio-mutation boundary is
now `audio::IEdit`, not `audio::IPlaybackContent`. `ITransport` remains the live
playback boundary. Undo/redo implementation remains out of scope for v20, but the
new `IEdit` stages should leave explicit TODO comments in the likely future
integration points for project-owned history and broader Tracktion edit commands.

Each stage should be implemented, reviewed, tested, and committed before moving to
the next stage. Keep compatibility shims until the stage that explicitly removes
them.

## Stage Order

1. Core Session Model:
   [v20-01](testable-modular-architecture-refactor-plan-v20-01-core-session.md)
2. Audio Transport Port:
   [v20-02](testable-modular-architecture-refactor-plan-v20-02-audio-transport-port.md)
3. Edit Port Scaffolding:
   [v20-03](testable-modular-architecture-refactor-plan-v20-03-playback-content-port.md)
4. Engine Port Adaptation:
   [v20-04](testable-modular-architecture-refactor-plan-v20-04-engine-port-adaptation.md)
5. Thumbnail Source API:
   [v20-05](testable-modular-architecture-refactor-plan-v20-05-thumbnail-source-api.md)
6. Editor Controller Contracts:
   [v20-06](testable-modular-architecture-refactor-plan-v20-06-editor-controller-contracts.md)
7. Editor Controller Behavior:
   [v20-07](testable-modular-architecture-refactor-plan-v20-07-editor-controller-behavior.md)
8. Waveform Row Component:
   [v20-08](testable-modular-architecture-refactor-plan-v20-08-waveform-row-component.md)
9. Transport Controls State:
   [v20-09](testable-modular-architecture-refactor-plan-v20-09-transport-controls-state.md)
10. Editor View Extraction:
    [v20-10](testable-modular-architecture-refactor-plan-v20-10-editor-view-extraction.md)
11. Editor Composition Wrapper:
    [v20-11](testable-modular-architecture-refactor-plan-v20-11-editor-composition-wrapper.md)
12. Main Window Rewire And Cleanup:
    [v20-12](testable-modular-architecture-refactor-plan-v20-12-main-window-rewire-and-cleanup.md)

## Commit Strategy

- One stage is one intended commit.
- Do not combine stages unless the code proves inseparable during implementation.
- Prefer adding new API beside old API first, then migrating consumers, then deleting
  legacy paths in stage 12.
- If a stage exposes a bad assumption, update that stage document before continuing.

## Verification Strategy

Use the narrowest reliable verification for the current stage:

- Core-only changes: core tests and a focused compile of touched core translation units.
- Audio adapter changes: compile the audio target or focused audio source commands.
- UI controller changes: headless UI tests where possible.
- JUCE component changes: focused compile first, then narrow component tests if the
  test target exists.

Every stage should add or update automated tests when there is behavior to verify.
For pure contract stages, add a small compile-backed contract test with fakes where
that provides value. Treat "compile only" as a fallback, not the default, and record
why no stronger automated test was practical in the stage commit message or notes.

The current Codex PowerShell environment may not have reliable `cmake`/`ctest`
access. If that remains true, use focused compile commands from
`build/debug/compile_commands.json` where possible and record that the full CMake
verification still needs to be run in a repaired environment.

## Non-Goals For v20

- No CMake target split.
- No separate `controllers` namespace.
- No `IThumbnailFactory`.
- No `ITransportControls`.
- No full multi-stem playback semantics.
- No undo/redo implementation yet.
- No JUCE dependency in `rock-hero-core`.
- No broad UI automation or end-to-end test suite.
