# Audio Engine Multi-Track Support

Status: planned. Do not implement until a feature needs independent playback tracks or stem
auditioning.

## Current Contract

`core::Session` may contain multiple tracks. That is a core model capability and should not be
restricted just because the first Tracktion adapter is simpler.

`audio::Engine` currently supports one backend playback track. `Engine::createTrack()` maps the
first valid `TrackId` to the single Tracktion audio track's `EditItemID`, allows clip creates for
that mapped id, and rejects later track creates for a different id. Successful clip creates map the
allocated `AudioClipId` to Tracktion's clip `EditItemID` and return identity-free
`AudioClipData` for `Session` to commit.

This is intentional for the current single-file editor workflow. It prevents the backend from
silently accepting session state that it cannot actually play independently.

## Goal

Add real Tracktion-backed support for multiple project tracks while keeping Tracktion and JUCE
details isolated inside `rock-hero-audio`.

The first multi-track implementation should still keep one clip per project track. Multiple clips
per track are a separate model/API expansion covered by
`docs/todo/multiple-audio-clips-plan.md`.

## Target Behavior

- `Session` can contain multiple tracks.
- `Engine` maps each created `TrackId` to a distinct Tracktion audio track.
- Creating a new clip for an existing `TrackId` replaces only that track's current clip.
- Creating a different `TrackId` creates or reuses that track's backend mapping before clips are
  loaded.
- Transport playback includes all loaded tracks.
- Seek clamping and automatic end-of-file stop use the maximum timeline end across all loaded
  backend clips.
- Failed clip creates for one track do not disturb other loaded tracks.
- Public interfaces continue to use project-owned `TrackId`, `TrackData`, `AudioClipId`,
  `AudioClipData`, `TimePosition`, and `TimeRange` values rather than Tracktion types.

## Non-Goals

- Do not expose Tracktion track handles outside `rock-hero-audio`.
- Do not make `core::Session` depend on audio backend capabilities.
- Do not add multi-clip-per-track storage as part of the first multi-track adapter slice.
- Do not introduce JUCE undo APIs into core or public Rock Hero interfaces.

## Design Notes

The adapter already keeps project-owned ids mapped to Tracktion `EditItemID`s internally. The
multi-track version should expand that one-track state into per-track backend state that includes:

- the stable Tracktion track identity
- the currently loaded `core::AudioClipData` returned for that project track
- the Tracktion clip identity needed to replace or remove the backend clip later

The current one-entry id maps and `m_loaded_length_seconds` fields should become aggregate backend
state. A simple first version can recalculate the loaded timeline end after each successful clip
create by walking the accepted clips stored in the adapter's mapping.

## Implementation Plan

1. Add tests first.
   - Creating clips on track 1 and track 2 both succeeds.
   - Replacing track 1 does not remove track 2.
   - Failed track 2 load leaves track 1 playable.
   - Transport seek clamps against the longest loaded track.
   - End-of-file stop happens at the longest loaded track end.

2. Replace the single-track binding.
   - Expand the current internal mapping keyed by `core::TrackId`.
   - Create or find a Tracktion audio track per created project track.
   - Keep `createAudioClip()` using the existing mapping rather than creating tracks implicitly.

3. Update loaded timeline tracking.
   - Replace `m_loaded_length_seconds` with an aggregate timeline end.
   - Recalculate after successful clip creates and future removals.
   - Keep position clamping centralized in the engine implementation.

4. Keep the public edit contract stable.
   - Keep returning `std::optional<core::AudioClipData>` until callers need structured
     errors.
   - Keep accepting a Session-allocated `AudioClipId` so the backend mapping and Session commit
     agree on durable identity.
   - Keep Tracktion failure details inside the adapter for now.

5. Update UI/controller assumptions.
   - `EditCoordinator` can continue asking `IEdit` to create tracks and clips by `TrackId`.
   - View state should continue reflecting all session tracks.
   - Any "currently auditioned track" or stem-specific UI policy should live above `Engine`.

## Open Questions

- Should the first multi-track adapter create Tracktion tracks immediately for every editor track,
  or should some future workflows support model-only tracks without backend mappings?
- Should track roles be explicit before multi-track playback lands, or is role-free stem playback
  enough for the first implementation?
- Should removal be implemented before multi-track loading, or can replacement-only behavior remain
  acceptable until the editor exposes track deletion?
- When structured load errors become useful, should `IEdit` move to
  `std::expected<core::AudioClipData, AudioLoadError>` before or during multi-track support?
