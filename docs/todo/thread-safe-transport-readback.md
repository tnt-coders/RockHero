# Thread-Safe Transport Readback (Deferred)

Status: deferred. Revisit when `rock-hero-game`, scoring, pitch analysis, or a future render
thread needs to read playback timing outside the JUCE message thread.

## Why This Exists

The current `ITransport` contract is message-thread owned. That is fine for editor controls and
for the current editor cursor overlay, because `juce::VBlankAttachment` runs on the message
thread. It is not enough for gameplay timing.

The architecture already calls out lock-free transport position as a future requirement. The game
view, scoring, and latency calibration should eventually derive timing from audio playback rather
than from the message loop, wall-clock time, or a render-frame accumulator.

This TODO records the intended design before the first gameplay/render consumer appears.

## Current State

`ITransport::state()` currently performs a live message-thread read of coarse transport state:

```cpp
TransportState{
    .playing = m_edit->getTransport().isPlaying(),
}
```

That is not thread-safe. Tracktion's `TransportControl::isPlaying()` reads
`TransportControl::TransportState::playing`, which is a `juce::CachedValue<bool>` backed by
Tracktion's transient `juce::ValueTree` state.

`ITransport::position()` currently reads:

```cpp
m_impl->m_edit->getTransport().getPosition().inSeconds()
```

That is also not thread-safe. Tracktion's public `TransportControl::getPosition()` reads another
`juce::CachedValue`. Rock Hero then clamps it against the adapter's plain loaded-length field.

This is acceptable as a temporary editor-only path because the editor view reads position on the
JUCE message thread. It should not become the gameplay/scoring timing source.

## Tracktion Timing Internals

Tracktion does have lower-level timing state that is designed for realtime use:

- `tracktion::graph::PlayHead` stores playhead data using atomics and seqlock-style storage.
- `EditPlaybackContext::audiblePlaybackTime` is an `std::atomic<double>`.
- `PlayHeadPositionNode` updates that atomic from the audio graph with latency compensation.
- `EditPlaybackContext::getAudibleTimelineTime()` loads `audiblePlaybackTime`.

The problem is not that an atomic value is unavailable somewhere inside Tracktion. The problem is
that the safe public access path is not a simple atomic read. Reaching the audible-time value
through `TransportControl::getCurrentPlaybackContext()` crosses Tracktion-owned pointer state whose
lifetime can change when the message thread rebuilds or frees the playback context. Going deeper to
the graph playhead uses internal APIs that Rock Hero consumers should never know about.

Therefore:

- do not expose Tracktion playback-context pointers to game, scoring, render, or UI code
- do not make non-audio code call `getCurrentPlaybackContext()` directly
- do not assume public `TransportControl::state`, `isPlaying()`, or `getPosition()` are atomic
- isolate any Tracktion timing read inside `rock-hero-audio`

## Design Decisions

Keep `seek()` on `ITransport`. It is a transport command because it changes the engine playhead
and therefore changes where playback will continue. It is not just a visual cursor movement.

Do not create an `ICursor` interface for `seek()` plus `position()`. Those methods have different
contracts:

- `seek()` is a side-effecting message-thread command
- live position readback is timing telemetry

Putting them together would create an interface where some methods are message-thread-only and
some are expected to be thread-safe. That mixed contract would be easy to misuse.

Prefer the name `IPlaybackClock` for the future read-only timing surface. "Cursor" is UI/editor
language and implies something visible or movable. The intended interface is broader: render,
scoring, latency calibration, debug timing, and future gameplay systems should all be able to read
the same playback-derived time source.

Do not put duration in the high-frequency clock. Duration is loaded-content or timeline-range
state, not realtime playback timing.

Duration also does not belong in `TransportState`. That snapshot should describe only the coarse
transport condition, such as whether playback is advancing. Loaded content duration now belongs in
session timeline state.

## Long-Term Public Shape

Keep `ITransport` as the message-thread command and coarse-state boundary:

```cpp
class ITransport
{
public:
    virtual void play() = 0;
    virtual void pause() = 0;
    virtual void stop() = 0;
    virtual void seek(core::TimePosition position) = 0;

    [[nodiscard]] virtual TransportState state() const noexcept = 0;

    virtual void addListener(Listener& listener) = 0;
    virtual void removeListener(Listener& listener) = 0;
};
```

`ITransport::position()` can remain as a temporary editor helper until the clock exists. Once
`IPlaybackClock` is available, remove public non-atomic position reads from `ITransport` unless a
specific message-thread-only consumer still justifies them.

Add a separate read-only playback clock:

```cpp
struct PlaybackClockSnapshot
{
    core::TimePosition position{};
    bool playing{false};
};

class IPlaybackClock
{
public:
    virtual ~IPlaybackClock() = default;

    [[nodiscard]] virtual PlaybackClockSnapshot snapshot() const noexcept = 0;
};
```

`Engine` can implement both `ITransport` and `IPlaybackClock`. The important rule is that
`IPlaybackClock::snapshot()` must not traverse Tracktion pointers, allocate, block, or call JUCE.
It should read only Rock Hero-owned atomic storage.

## Timeline Concepts

Do not create one broad `ITimeline` just to expose `start()` and `end()`. A timeline range is a
value object, not a runtime boundary:

```cpp
struct TimeRange
{
    core::TimePosition start{};
    core::TimePosition end{};
};
```

Use more specific owning concepts around that value:

- `ProjectTimeline` for the full project/song range and tempo map
- `TimelineViewport` for the currently visible editor range
- `PlaybackTimelineState` for loaded playable content bounds

Tempo-map data and conversion math should live in `rock-hero-core` as pure logic. Zoom and visible
range are editor/UI presentation state. Loaded playable bounds belong to audio/editor state, not to
the transport clock.

An `ITimeline` interface could become worthwhile later if timeline behavior crosses a runtime or
framework boundary. Examples include hiding Tracktion's tempo sequence behind a project-owned
tempo-aware interface, or allowing scoring/editor simulations to use a fake timeline with the same
beat/time conversion API as a production timeline. Do not introduce it for simple range storage.

## Field Analysis

### Position

Position is the primary reason for `IPlaybackClock`. It should be available as a non-blocking read
from consumers that need to render, score, or analyze against playback time.

The value should represent audible timeline position when possible, not merely the control-surface
cursor. Tracktion already calculates an audible timeline value in `PlayHeadPositionNode` by
accounting for graph latency before writing `EditPlaybackContext::audiblePlaybackTime`.

### Playing

`playing` is likely useful, but it is secondary. Gameplay may use it to gate scoring, pause note
spawning, or distinguish a valid stationary cursor from active playback.

It should be treated as "the transport is advancing or expected to advance" rather than as a full
transport-state replacement. Button enabledness, load state, loop state, errors, and duration
should continue to come from message-thread state.

### Duration

Duration should not be in `PlaybackClockSnapshot`.

Duration is edit/load state, not realtime playback timing. It changes when content is loaded or
edited, not on every audio block. UI and game systems that need duration for coordinate mapping
should receive it through timeline state, session data, or a separate message-thread snapshot.

`TransportState` no longer carries duration. Continue keeping duration in loaded timeline or
content state rather than in realtime playback snapshots.

## Recommended Storage

Start with an Engine-owned atomic mirror rather than direct Tracktion reads from consumers.

Use integer timeline ticks for the hot position storage instead of `std::atomic<double>`:

```cpp
class AtomicPlaybackClock final : public IPlaybackClock
{
public:
    [[nodiscard]] PlaybackClockSnapshot snapshot() const noexcept override;

    void publishPosition(core::TimePosition position) noexcept;
    void publishPlaying(bool playing) noexcept;

private:
    std::atomic<std::int64_t> m_position_nanoseconds{0};
    std::atomic<bool> m_playing{false};
};
```

Nanosecond ticks are much finer than audio sample resolution and avoid relying on lock-free
floating-point atomics. They also keep the read side simple:

```cpp
const auto ns = m_position_nanoseconds.load(std::memory_order_relaxed);
const bool playing = m_playing.load(std::memory_order_relaxed);
```

`memory_order_relaxed` is appropriate if the snapshot is treated as telemetry and does not publish
ownership or lifetime of any other data. If future code needs the clock read to synchronize with
other state, that should be a separate design decision, not an accidental side effect of this API.

This first version does not guarantee that `position` and `playing` come from the exact same audio
block. That is acceptable for render and most gating uses. If gameplay later requires a coherent
multi-field timing record, replace the internal storage with a project-owned seqlock snapshot and
keep the public `IPlaybackClock` API unchanged.

## Publishing Strategy

The final implementation should publish from the audio-derived timing path, not from consumer
threads.

Preferred source order:

1. Audio-graph timing source that writes Rock Hero's mirror during playback processing.
2. Adapter-owned read of Tracktion audible time, if lifetime can be proven safe.
3. Message-thread polling only as a temporary display-only fallback.

Avoid this design outside `rock-hero-audio`:

```cpp
auto* context = transport.getCurrentPlaybackContext();
return context->getAudibleTimelineTime();
```

That reads an atomic value after traversing Tracktion-owned pointers whose lifetime is not part of
a thread-safe Rock Hero contract.

The ideal long-term implementation is to publish Rock Hero's atomic clock from the same place
Tracktion updates audible playback time. That may require one of:

- a small Tracktion-supported hook around `EditPlaybackContext::audiblePlaybackTime`
- a project-owned graph/tap node inserted into the playback graph
- a carefully isolated Tracktion submodule patch, if no public hook exists

Do not make game, scoring, render, or UI code aware of which option was chosen.

Message-thread transport operations should also publish immediate boundary values:

- construction publishes `position = 0`, `playing = false`
- loading new content publishes the reset position
- `seek()` publishes the requested/clamped position
- `play()` publishes `playing = true`
- `pause()` publishes `playing = false`
- `stop()` publishes `position = 0`, `playing = false`
- automatic end-of-file stop publishes the final stopped state

Those message-thread publishes keep the clock useful before the audio graph has emitted its first
block and after playback has stopped.

## Implementation Timing

Do not build the atomic playback clock immediately just for the editor. The editor can keep using
the message-thread path while it remains a JUCE message-thread component.

Add `IPlaybackClock` when one of these appears:

- `rock-hero-game` needs playback-derived timing
- scoring needs stable timing windows
- latency calibration samples playback time
- render or analysis code runs outside the JUCE message thread
- debug timing instrumentation needs a shared playback time source

The first implementation may be a message-thread mirror if that unblocks structure. Design it so
the publisher can later move to an audio-derived source without changing consumers.

## Consumer Guidance

Use `IPlaybackClock` for:

- render-frame cursor/highway position
- scoring-time lookup
- latency-calibration sampling
- debug timing instrumentation

Use `ITransport` for:

- play, pause, stop, and seek commands
- message-thread UI state
- coarse state listener callbacks

Use timeline/content state for:

- duration
- loaded playable range
- project/song range
- visible editor range
- tempo-aware time/beat conversion

Do not infer duration from the clock. Do not drive command behavior from the clock when the
message-thread transport state is available.

## Testing Plan

Add pure unit tests for `AtomicPlaybackClock`:

- default snapshot is zero and stopped
- published position is returned exactly after tick conversion
- published playing state is returned
- position and playing can be updated independently

Add unit tests for timeline values when they are introduced:

- `TimeRange` duration calculation
- clamping positions to a range
- visible range mapping from time to normalized/pixel coordinates
- tempo-map conversions in `rock-hero-core`

Add adapter tests for `Engine` when the clock interface is introduced:

- construction exposes a zero stopped clock snapshot
- loading content leaves the clock stopped at the start
- seeking publishes the clamped position
- play/pause/stop publish the expected playing flag
- stop publishes position zero

Add a focused stress test only if useful. A simple many-reader/many-writer test can prove the
Rock Hero storage has no obvious tearing, but it is not a substitute for ThreadSanitizer or for
audio-timing integration tests.

When gameplay timing exists, add a replayable simulation test that feeds synthetic clock snapshots
into scoring logic. Scoring correctness should not require Tracktion, JUCE, an audio device, or a
message loop.

## Migration Plan

1. Keep `ITransport` documented as message-thread-only.
2. Keep duration out of `TransportState`; loaded content length belongs in timeline state.
3. Extend `TimeRange` and pure timeline/viewport mapping where editor zoom needs it.
4. Add `PlaybackClockSnapshot` and `IPlaybackClock` when gameplay/scoring timing needs it.
5. Add the Engine-owned atomic mirror and implement `IPlaybackClock` on `Engine`.
6. Publish boundary values from existing message-thread transport operations.
7. Investigate the safest audio-derived publish hook inside the Tracktion adapter.
8. Move future game/render/scoring consumers to depend on `IPlaybackClock`, not `ITransport`.
9. Remove `ITransport::position()` if no remaining message-thread-only consumer justifies it.
10. Optionally move the editor cursor overlay from `ITransport::position()` to `IPlaybackClock`.

Step 10 is optional because the current editor cursor read is still message-thread based. The game
and scoring paths are the real reason for this work.

## Open Questions

- Can Tracktion expose a stable readback hook for audible playback time without a submodule patch?
- Does gameplay need `playing`, or is a monotonically sampled position enough?
- Should future sample-accurate scoring use timeline seconds, timeline sample positions, or both?
- Will the render path run on the JUCE message thread, a bgfx render thread, or both?
- Is one-frame `playing`/`position` incoherence acceptable for every first consumer?
- Should Rock Hero store clock position in nanoseconds, samples, or both?
- What is the first concrete owner for loaded playback range: audio, editor logic, or session?

The likely answer today: position is required, playing is useful enough to include, duration should
stay out of the realtime clock, and the clock should wait until a real timing-sensitive consumer
appears.
