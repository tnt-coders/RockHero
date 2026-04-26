# v20 Stage 08 - Track View

## Goal

Prepare waveform rendering for multi-track-shaped view state while keeping the first
implementation as one row.

The cursor is owned by an editor-wide overlay component introduced in stage 10,
not by `TrackView`. The cursor is sourced via a vsync pull from
`audio::ITransport` (see stage 06's Cursor Architecture note and stage 10).
Track views render only static waveform content; they neither store cursor state nor
animate.

## Expected Files

- `libs/rock-hero-ui/include/rock_hero/ui/editor/track_view.h`
- `libs/rock-hero-ui/src/track_view.cpp`
- existing `waveform_display` files as temporary compatibility wrappers if needed
- possible narrow UI tests for track-view state behavior

## Implementation Steps

1. Add `TrackView` as a JUCE component that owns one
   `std::unique_ptr<audio::Thumbnail>`.
2. Add `setThumbnail(std::unique_ptr<audio::Thumbnail>)`.
3. Add `setState(const TrackViewState&)`.
4. Diff the optional `AudioAsset` against the previously applied asset.
5. Call `Thumbnail::setSource(asset)` only when the asset changes to a present value.
6. Draw only track-local waveform content from `TrackViewState`.
7. Do not draw the playhead cursor in `TrackView`. The editor has one
   window-wide cursor overlay across all waveform rows; `EditorView` owns that
   overlay (see stage 10).
8. Keep track-view responsibilities limited to waveform rendering, local hit
   testing, and local asset refresh. Do not add transport listeners, cursor
   timers, or per-frame animation logic to the view.
9. Emit waveform-click intent to the parent through a local listener or
   equivalent local callback, keeping public callback members out of the design.
10. Leave existing `WaveformDisplay` in place until `EditorView` replaces it.

## Tests

Add a narrow automated test with a fake thumbnail if the JUCE test harness can
construct the component. If direct JUCE component construction is not practical yet,
extract the asset-diff decision into a tiny helper and test that helper in this
stage rather than leaving the behavior untested.

- setting a present new asset calls `setSource(...)`,
- setting the same asset twice does not call `setSource(...)` twice,
- changing to another asset calls `setSource(...)` again,
- row click reports a normalized position.

Compile-only verification is acceptable only if neither JUCE component construction
nor a small extracted helper is practical; document that limitation explicitly.

## Verification

Compile `rock-hero-ui` sources that include the new row. Run narrow UI tests if they
exist.

## Exit Criteria

- Thumbnail refresh is automatic from track-view state changes.
- `TrackView` owns its thumbnail.
- `TrackView` does not own, store, or draw playhead cursor state.
- `TrackView` does not become the owner of smooth cursor animation logic.
- Existing editor UI still works through the old waveform path.

## Do Not Do

- Do not add dynamic track creation UI.
- Do not add `IThumbnailFactory`.
- Do not remove `WaveformDisplay` yet.
