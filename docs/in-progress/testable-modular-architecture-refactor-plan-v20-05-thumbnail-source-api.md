# v20 Stage 05 - Thumbnail Source API

## Goal

Move thumbnail source assignment from `juce::File` to `core::AudioAsset` while
keeping thumbnail drawing in the audio adapter.

## Expected Files

- `libs/rock-hero-audio/include/rock_hero/audio/thumbnail.h`
- `libs/rock-hero-audio/src/tracktion_thumbnail.h`
- `libs/rock-hero-audio/src/tracktion_thumbnail.cpp`
- `libs/rock-hero-audio/tests/test_thumbnail_source.cpp` if an audio test target
  exists or is added
- current UI code that calls `Thumbnail::setFile(...)`

## Implementation Steps

1. Replace `Thumbnail::setFile(const juce::File&)` with
   `Thumbnail::setSource(const core::AudioAsset&)`.
2. Include or forward-declare only what is needed in `thumbnail.h`; do not expose
   Tracktion.
3. Convert `core::AudioAsset` to framework file objects inside the concrete
   Tracktion thumbnail adapter.
4. Update current waveform UI call sites to use `setSource(...)`.
5. Keep `Engine::createThumbnail(juce::Component&)` unchanged.

## Tests

Add or update automated tests proving thumbnail source assignment uses
`core::AudioAsset`.

Prefer:

- an adapter test for `TracktionThumbnail::setSource(...)` when Tracktion setup is
  practical,
- otherwise a compile-backed fake-thumbnail test proving UI-facing code can call
  `Thumbnail::setSource(const core::AudioAsset&)` without JUCE file conversion at the
  call site.

Also update any existing tests or fixtures that still call `setFile(...)`.

## Verification

Run the thumbnail test if available, then compile audio and UI sources that include
`thumbnail.h` or call thumbnail source assignment.

## Exit Criteria

- UI code no longer passes `juce::File` directly into `audio::Thumbnail`.
- `audio::Thumbnail` remains a translation adapter, not a new port.
- No concrete `audio::Engine` dependency is added to new UI code.

## Do Not Do

- Do not introduce `IThumbnailFactory`.
- Do not convert thumbnails into pure waveform data.
- Do not change thumbnail ownership yet beyond what current callers require.
