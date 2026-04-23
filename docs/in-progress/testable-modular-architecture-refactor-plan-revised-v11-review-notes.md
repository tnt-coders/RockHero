# Review Notes: Testable Modular Architecture Refactor Plan Revised v11

Review target:
`docs/in-progress/testable-modular-architecture-refactor-plan-revised-v11.md`

## Findings

### High: The plan loses the waveform source update after a successful load

`EditorViewState` carries only `loaded_asset_label`, not the loaded `AudioAsset`, and the load flow
says success is represented by the next transport state. Step 5 only gives `WaveformDisplay`
`attachThumbnail(...)` plus seek output. There is no replacement for today's accepted-load path,
where `MainWindow::ContentComponent` calls `m_waveform_display.setAudioFile(file)` only after
`loadFile` succeeds.

As written, the controls can become enabled while the waveform never receives
`Thumbnail::setSource(...)`. This also undermines the v11 cache-seeding rule: a presenter
constructed after a prior load cannot derive `loaded_asset_label` or waveform source from the
proposed `TransportState`, because `TransportState` has no asset identity.

Add a loaded asset/source field to the state path, or define an explicit successful-load source
update path that is still non-speculative.

### High: Removing `WaveformDisplay`'s engine listener leaves no cursor update API

The plan puts `cursor_proportion` in `EditorViewState` and removes `WaveformDisplay`'s
`Engine::Listener` inheritance in step 5, but step 5 only adds thumbnail attachment and normalized
seek output. Step 7 says `EditorView::setState` projects state into `TransportControlsState`, with
no explicit `WaveformDisplay::setCursorProportion(...)` or equivalent.

Today the cursor is driven by `WaveformDisplay::engineTransportPositionChanged`; after the
refactor, that path is gone and the cursor can freeze unless the plan adds the replacement setter
and forwards it from `EditorView::setState`.

### Medium: Successful load notification is relied on but not made an engine requirement

The load flow assumes that on success "the next `onTransportStateChanged`" carries
`file_loaded = true` and `length`. Step 2 says `Engine` implements `ITransport` and dual-dispatches,
but it does not explicitly require `loadAudioAsset(...)` to publish a fresh `TransportState` after
a successful load.

The current `loadFile` mutates loaded length and sets position to zero. If the transport was
already stopped at zero, existing Tracktion callbacks may not produce any visible state event. The
plan should state that `loadAudioAsset` emits or schedules a state snapshot on success even when
play state and position are unchanged, and tests should cover that exact case.

## Open Question

Should `TransportState` include `std::optional<core::AudioAsset>` or a display label, or should the
editor presenter maintain the last accepted asset separately? The former makes constructor seeding
and future restore-session behavior match the v11 intent more directly.
