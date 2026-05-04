# Audio Asset Catalog and Thumbnail Cache

Status: planned. Revisit when clips can share assets or when duplicate waveform generation becomes
observable enough to justify the model change.

## Goal

Allow multiple clips and tracks to reference the same audio asset without duplicating asset
metadata in every `AudioClip`, and without making JUCE views own project data. The durable project
model should distinguish:

- the audio assets imported into the project
- the clips that reference those assets
- the thumbnails or proxy data used to render those assets

The main architectural constraint is that `rock-hero-core` remains framework-free. Asset identity
and clip references belong in core. Tracktion, JUCE, waveform proxies, and repaint wiring stay in
adapter or UI boundary code.

## Current State

The current model is intentionally simple:

- `core::AudioAsset` stores a filesystem path.
- `core::AudioClip` stores a full `AudioAsset` value directly.
- `ui::AudioClipViewState` also carries the full `AudioAsset`.
- `ui::TrackView` creates one `AudioClipView` child per projected clip state.
- `AudioClipView` owns one `audio::IThumbnail` adapter and refreshes its source when the asset
  value changes.
- `audio::IThumbnailFactory::createThumbnail()` binds each thumbnail adapter to a JUCE owner
  component for repaint behavior.

This is acceptable while the editor only imports one asset into one clip. It becomes less ideal
once multiple clips can share one source file because the model duplicates path identity and the
view layer has no explicit asset id to use for cache lookups.

## Target Model

The eventual core shape should be asset-catalog based:

```cpp
struct AudioAssetId
{
    std::uint64_t value{0};
};

struct AudioAsset
{
    AudioAssetId id;
    std::filesystem::path path;
    TimeDuration duration;
};

struct AudioClip
{
    AudioClipId id;
    AudioAssetId asset_id;
    TimeRange source_range;
    TimePosition position;
};

class Session
{
public:
    std::span<const AudioAsset> audioAssets() const noexcept;
    const AudioAsset* findAudioAsset(AudioAssetId id) const noexcept;
    const AudioAsset* findAudioAssetByPath(const std::filesystem::path& path) const;
};
```

`AudioClip` should store an id, not a C++ reference or pointer. Clips are value types that need to
remain copyable, comparable, serializable, and stable across container moves.

## Ownership Rules

The editor view should not own the project asset list. The asset catalog belongs to
`core::Session` or a future framework-free editor/project model. `EditorController` should project
that model into view state.

The UI should not retrieve project data upward from `EditorView`. `AudioClipView` should remain a
presentation component that receives state and an already-created thumbnail adapter.

Thumbnail creation may be coordinated at the editor view boundary, but thumbnail caching should not
make `EditorView` a data owner. A better split is:

- `core::Session` owns asset identity and clip references.
- `ui::EditorController` resolves session data into view state.
- `ui::TrackView` or `AudioClipView` receives a thumbnail provider dependency.
- `rock-hero-audio` owns the concrete Tracktion/JUCE thumbnail and proxy implementation.
- The concrete thumbnail provider/cache shares backend proxy data by `AudioAssetId` or canonical
  asset path while still returning view-owned thumbnail adapters where needed.

## Thumbnail Sharing Direction

Sharing an audio asset does not necessarily mean sharing one `IThumbnail` object. The current
factory binds thumbnails to owner components so the backend can repaint the correct view when proxy
generation progresses. That argues for one view-owned thumbnail adapter per `AudioClipView`.

What should be shared is lower-level asset/proxy state:

- decoded file metadata
- generated waveform proxy data
- cache entries keyed by asset id or canonical path

The public boundary can evolve from `IThumbnailFactory` toward a provider that accepts the asset
identity explicitly:

```cpp
class IThumbnailProvider
{
public:
    virtual ~IThumbnailProvider() = default;

    [[nodiscard]] virtual std::unique_ptr<IThumbnail> createThumbnail(
        core::AudioAssetId asset_id, juce::Component& owner) = 0;
};
```

If Tracktion still requires `setSource()` on each adapter, the provider can create the adapter and
prime it internally. The UI does not need to know whether the backend shared an existing proxy or
started a new one.

## View-State Direction

Once `AudioClip` stores `AudioAssetId`, `AudioClipViewState` should carry asset identity rather
than a full asset path:

```cpp
struct AudioClipViewState
{
    core::AudioClipId audio_clip_id;
    core::AudioAssetId audio_asset_id;
    core::TimeRange source_range;
    core::TimeRange timeline_range;
};
```

Only add denormalized asset display fields to view state when the UI actually needs them. For
example, a future clip label could carry `asset_display_name`, but waveform rendering should only
need the asset id and clip ranges.

## Implementation Plan

1. Add core asset identity.
   - Add `core::AudioAssetId`.
   - Decide whether `AudioAsset` stores its id directly or whether the catalog pairs ids with
     identity-free asset data.
   - Add invalid-id semantics and equality tests.

2. Add a session-owned asset catalog.
   - Add read-only `audioAssets()` access.
   - Add `findAudioAsset(AudioAssetId)` and path lookup helpers.
   - Add an import/register method that deduplicates by canonical path when appropriate.
   - Keep mutation behind `Session` or `EditCoordinator`; do not expose mutable containers.

3. Change clips to reference assets by id.
   - Replace `AudioClipSpec::asset` and `AudioClip::asset` with `asset_id`.
   - Keep duration and source-range validation based on the resolved asset.
   - Update `Session::setAudioClip` or future `addAudioClip` APIs to reject missing asset ids.
   - Update timeline calculations to remain clip-based.

4. Update the edit coordination boundary.
   - On import, register or reuse an asset id before creating the backend clip.
   - Pass resolved `AudioAsset` data to `IEdit` because the backend still needs a path.
   - Commit clips with the accepted `AudioAssetId`, not a duplicated path.
   - Ensure failed backend loads do not leave orphaned catalog entries unless an explicit
     "imported but unused" policy has been chosen.

5. Update view-state projection.
   - Project `AudioClip::asset_id` into `AudioClipViewState`.
   - Keep `EditorViewState` framework-free and derived from the session.
   - Avoid letting `AudioClipView` query `Session` or `EditorView` for asset data.

6. Introduce thumbnail provider semantics.
   - Rename or replace `IThumbnailFactory` only when the asset-id boundary is ready.
   - Prefer `createThumbnail(asset_id, owner)` over a view calling `setSource(path)`.
   - Keep each `AudioClipView` owning its adapter so component teardown order stays local.
   - Let the audio implementation share proxy/cache state internally.

7. Update tests.
   - Add pure core tests for asset id allocation, path deduplication, lookup, and missing ids.
   - Add session tests proving clips can share one asset id.
   - Add coordinator tests proving a reused path reuses the asset id.
   - Add UI tests proving `AudioClipViewState` carries asset ids and thumbnail creation receives
     the expected id.
   - Add audio adapter/provider tests around thumbnail cache reuse if the concrete backend exposes
     observable cache behavior through project-owned interfaces.

## Non-Goals

- Do not put the asset catalog in `EditorView`.
- Do not make `AudioClip` store C++ references or pointers to assets.
- Do not expose Tracktion or JUCE thumbnail/cache objects through core or UI view state.
- Do not require all clips sharing an asset to share the same `IThumbnail` adapter instance.
- Do not combine this with full multi-track playback unless a single feature slice requires both.

## Open Questions

- Should asset deduplication use canonical filesystem paths immediately, or should exact stored
  paths remain distinct until project packaging rules exist?
- Should unused imported assets remain in the session catalog, or should a failed/removed last clip
  remove the asset automatically?
- Should `AudioAssetId` be persisted as stable song data immediately, or remain session-local until
  song serialization includes editor clip state?
- Should thumbnail cache invalidation be based on asset id only, or also include file timestamp,
  size, hash, or source-range-independent proxy settings?
- Should a future asset catalog live directly in `Session`, or should `Session` eventually become
  part of a larger `Project` aggregate?

## Recommended First Slice

Do not start by moving thumbnail lookup into `EditorView`. The first useful slice should be pure
core model work:

- add `AudioAssetId`
- add a session-owned asset catalog
- keep clip creation working for the current single-clip editor path
- change clips and clip view state to carry `AudioAssetId`
- update tests proving two clips can reference the same asset id

After that, replace the thumbnail factory contract with an asset-id-aware provider and let the
audio adapter decide how much proxy data can be shared behind the project-owned interface.
