# Thread-Safe Transport Readback (Deferred)

Status: deferred. Revisit when `rock-hero-game`, scoring, pitch analysis, or a future render thread
needs to read transport timing outside the JUCE message thread.

## Why This Exists

The current `ITransport` contract is message-thread owned. That is fine for editor controls and for
the current editor cursor overlay, because `juce::VBlankAttachment` runs on the message thread.
It is not enough for gameplay timing.

The architecture already calls out lock-free transport position as a future requirement. The game
view, scoring, and latency calibration should eventually derive timing from audio playback rather
than from the message loop, wall-clock time, or a render-frame accumulator.

This TODO records the intended design before the first gameplay/render consumer appears.

## Current State

`ITransport::status()` currently returns a coarse message-thread snapshot:

```cpp
TransportStatus{
    .playing = m_edit->getTransport().isPlaying(),
    .duration = core::TimeDuration{m_loaded_length_seconds},
}
```

That is not thread-safe. Tracktion's `TransportControl::isPlaying()` reads cached
`juce::ValueTree` state, and `m_loaded_length_seconds` is a plain `double`.

`ITransport::position()` currently reads:

```cpp
m_impl->m_edit->getTransport().getPosition().inSeconds()
```

That is also not thread-safe. Tracktion's public `TransportControl::getPosition()` reads another
cached `ValueTree` value, then Rock Hero clamps it against the same plain duration field.

Tracktion does have lower-level timing state that is designed for realtime use:

- `tracktion::graph::PlayHead` stores playhead data with atomics and seqlocks.
- `EditPlaybackContext::audiblePlaybackTime` is an `std::atomic<double>`.
- `PlayHeadPositionNode` updates that atomic from the audio graph with latency compensation.

The problem is not the value itself. The problem is the access path. Reaching it through
`TransportControl::getCurrentPlaybackContext()` crosses a raw pointer graph whose lifetime can be
changed by message-thread transport operations. Rock Hero consumers should not depend on that.

## Design Goal

Add a project-owned, read-only playback timing port that can be used from non-message threads
without exposing Tracktion internals.

The high-frequency read contract should be:

- no Tracktion or JUCE types in the public API
- no command methods on the readback interface
- no listener registration on the readback interface
- no locks, allocation, or framework pointer traversal on read
- cheap enough for render-frame and scoring reads
- fakeable in deterministic tests

Keep `ITransport` as the message-thread command/status boundary:

```cpp
play()
pause()
stop()
seek(position)
status()
position()
addListener()
removeListener()
```

Do not turn `ITransport` itself into the cross-thread timing interface. It mixes command methods,
listener lifetime, edit-visible duration state, and message-thread Tracktion calls. Making only
some methods thread-safe would make the contract harder to reason about.

## Recommended Public Shape

Add a separate read-only interface, probably in `rock-hero-audio`:

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

`Engine` can implement both `ITransport` and `IPlaybackClock`.

Use `ITransport` for message-thread control and state transitions. Use `IPlaybackClock` for
render/scoring reads that need live playback timing.

## Field Analysis

### Position

Position is the primary reason for this interface. It should be available as a non-blocking read
from any consumer thread that needs to render, score, or analyze against playback time.

The value should represent the audible timeline position when possible, not merely the control
surface cursor. Tracktion already calculates this in `PlayHeadPositionNode` by accounting for graph
latency before writing `EditPlaybackContext::audiblePlaybackTime`.

### Playing

`playing` is likely useful, but it is secondary. Gameplay may use it to gate scoring, pause note
spawning, or distinguish a valid stationary cursor from active playback.

It should be treated as "the transport is advancing or expected to advance" rather than as a full
transport-status replacement. Button enabledness, load state, loop state, errors, and duration
should continue to come from message-thread state.

### Duration

Do not put duration in the high-frequency clock snapshot initially.

Duration is edit/load state, not realtime playback timing. It changes when content is loaded or
edited, not on every audio block. UI and game systems that need duration for coordinate mapping
should receive it through ordinary state projection, session data, or a separate message-thread
snapshot.

Adding duration to the realtime clock would force unnecessary multi-field consistency questions
onto a value that does not need to be read at realtime cadence.

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
2. A safe Tracktion-provided readback hook, if Tracktion exposes one with stable lifetime rules.
3. Message-thread polling only as a temporary display-only fallback.

Avoid this design:

```cpp
auto* context = transport.getCurrentPlaybackContext();
return context->getAudibleTimelineTime();
```

That reads an atomic value after traversing Tracktion-owned raw pointers whose lifetime is not part
of a thread-safe Rock Hero contract.

The ideal long-term implementation is to publish Rock Hero's atomic clock from the same place
Tracktion updates audible playback time. That may require one of:

- a small Tracktion-supported hook around `EditPlaybackContext::audiblePlaybackTime`
- a project-owned graph/tap node inserted into the playback graph
- a carefully isolated Tracktion submodule patch, if no public hook exists

Do not make the game or scoring code aware of which option was chosen.

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

## Consumer Guidance

Use `IPlaybackClock` for:

- render-frame cursor/highway position
- scoring-time lookup
- latency-calibration sampling
- debug timing instrumentation

Use `ITransport` for:

- play, pause, stop, and seek commands
- message-thread UI state
- duration and load-dependent timeline mapping
- coarse status listener callbacks

Do not infer duration from the clock. Do not drive command behavior from the clock when the
message-thread transport status is available.

## Testing Plan

Add pure unit tests for `AtomicPlaybackClock`:

- default snapshot is zero and stopped
- published position is returned exactly after tick conversion
- published playing state is returned
- position and playing can be updated independently

Add adapter tests for `Engine` when the interface is introduced:

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

1. Clarify `ITransport` documentation so `position()` is explicitly message-thread-only today.
2. Add `PlaybackClockSnapshot` and `IPlaybackClock` as project-owned audio interfaces.
3. Add the Engine-owned atomic mirror and implement `IPlaybackClock` on `Engine`.
4. Publish boundary values from existing message-thread transport operations.
5. Investigate the safest audio-derived publish hook inside the Tracktion adapter.
6. Move future game/render/scoring consumers to depend on `IPlaybackClock`, not `ITransport`.
7. Optionally move the editor cursor overlay from `ITransport::position()` to `IPlaybackClock`.

Step 7 is optional because the current editor cursor read is still message-thread based. The game
and scoring paths are the real reason for this work.

## Open Questions

- Can Tracktion expose a stable readback hook for `audiblePlaybackTime` without a submodule patch?
- Does gameplay need `playing`, or is a monotonically sampled position enough?
- Should future sample-accurate scoring use timeline seconds, timeline sample positions, or both?
- Will the render path run on the JUCE message thread, a bgfx render thread, or both?
- Is one-frame `playing`/`position` incoherence acceptable for every first consumer?

The likely answer today: position is required, playing is useful enough to include, and duration
should stay out of the realtime clock until a real consumer proves otherwise.
