# macOS Seek Regression Investigation

## Summary

There is a macOS-only runtime crash in the concrete Tracktion-backed `audio::Engine` adapter when
tests call `ITransport::seek(core::TimePosition)` after loading audio into the edit.

This is not the same issue as the recent clang-tidy failures in UI tests. The failing tests are
runtime integration tests in `libs/rock-hero-audio/tests/test_engine.cpp`.

## Known Failing Tests

From macOS CI `ctest --preset release --output-on-failure`:

- `Engine seek updates transport state synchronously`
- `Engine seek notifies position listeners when position changes`

The second test was later renamed to:

- `Engine seek keeps transport listeners coarse while legacy position listeners still fire`

At the time of the failure, both crashes occurred immediately after:

```cpp
transport.seek(core::TimePosition{target_seconds});
```

and were reported as `SIGSEGV`.

## Current Suspect Area

The concrete seek path is:

- `libs/rock-hero-audio/src/engine.cpp`
- `Engine::seek(core::TimePosition position)`
- `tracktion::TransportControl::setPosition(...)`

The crash does **not** look like a generic "macOS cannot seek" problem. It looks like a
regression introduced by changes to how `Engine` integrates with Tracktion transport state and
position callbacks.

## Relevant Commits

### CI observation from later investigation

#### `ed9139f` - `Remove cursor_proportion (no longer needed in updated design)`

Date: April 25, 2026

Observed status:

- macOS build reportedly succeeded at this commit

Why it matters:

- This provides a concrete "known good" point later in history than the earlier broad regression
  guess.
- This commit changes plan docs, `EditorViewState`, and the headless UI controller test. It does
  not touch `rock-hero-audio`.

#### `fe2ee4d` - `Fixed clang-tidy issues`

Date: April 25, 2026

Observed status:

- macOS build reportedly started failing at this commit

Why it matters:

- This is a very tight commit-to-commit transition after `ed9139f`.
- However, the diff only changes `libs/rock-hero-ui/tests/test_editor_controller.cpp`.
- It does **not** touch `libs/rock-hero-audio/src/engine.cpp`,
  `libs/rock-hero-audio/tests/test_engine.cpp`, or the concrete Tracktion transport code path.

Implication:

- If the macOS audio seek crash really first appears at `fe2ee4d`, then the failure may be:
  - caused by a factor other than the committed audio code in that exact diff,
  - a latent or nondeterministic failure that was merely first observed there,
  - or a CI attribution/bisect issue that needs re-checking.

#### `3fa844d` - `Fixed linter issues on macOs`

Date: April 26, 2026

Observed status:

- latest commit reportedly works again

Why it matters:

- This makes the issue look non-monotonic rather than like a clean regression introduced by
  `fe2ee4d`.
- That increases the likelihood of:
  - a flaky or timing-sensitive runtime bug,
  - CI or environment variation on macOS,
  - or a latent defect whose visibility depends on build shape or scheduling.

### Earlier code changes that still warrant comparison

#### `134d6a5` - `Replace transport polling with event-driven Engine subscriptions`

Date: April 19, 2026

Why it matters:

- `Engine` stopped using the old polling-style cursor/state propagation model.
- `Engine::Impl` started listening directly to Tracktion transport events:
  - `juce::ChangeListener` for play state
  - `juce::ValueTree::Listener` for `tracktion::IDs::position`
- `seek(double)` began relying on Tracktion-driven callback publication rather than a simpler
  cache-first model.
- This is the point where stopped seeks began to depend much more on Tracktion's internal
  transport callback behavior.

Relevant change themes from the commit message:

- "Replace transport polling with event-driven Engine subscriptions"
- "reacts to the transport ValueTree's IDs::position property for position updates"

#### `e21f699` - `Refine transport stop-state handling`

Date: April 20, 2026

Why it matters:

- Removed manual position publication after `seek(...)`.
- Removed manual play/pause publication after transport operations.
- Relied more heavily on Tracktion's own callbacks to report the result of `stop`, `play`,
  `pause`, and `seek`.
- Kept natural end-of-song behavior by letting Tracktion callbacks observe the state changes after
  transport mutations.

This made `Engine` more dependent on Tracktion's stopped-state transport behavior and on our
`ValueTree` listener integration being safe and non-reentrant.

### Later commits that are probably not the root cause, but are relevant context

#### `25714c2` - `Simplified IEdit contract and validated natural transport notifications`

Date: April 24, 2026

Why it matters:

- Reinforced the "natural Tracktion notifications" direction.
- Added integration tests that expect synchronous visible transport behavior from the real engine.
- Did **not** obviously introduce the crash by itself, but it locked in the assumption that the
  Tracktion-backed callback path was safe.

#### `24aa872` - `Make Transport Listeners Coarse For Step 7`

Date: April 25, 2026

Why it matters:

- Changed only the project-owned `ITransport::Listener` notification policy so position-only
  changes no longer trigger `onTransportStateChanged(...)`.
- Kept legacy `Engine::Listener::engineTransportPositionChanged(...)` behavior.
- This commit is probably **not** the root cause of the macOS segfault, but it changed the seek
  test expectations and should be kept in mind when reproducing later.

## Main Hypotheses

These were not proven yet. They are the most plausible explanations based on the changed code
path.

### Hypothesis 1: stopped seek now touches Tracktion playback-context logic

Tracktion `TransportControl::setPosition(...)` is very small, but the surrounding stopped-state
transport behavior can involve playback-context and playhead machinery when the edit keeps audio
active while stopped.

This suggests an interaction with:

- `Edit::playInStopEnabled`
- `TransportControl::stop(...)`
- `TransportControl::performPositionChange()`
- `playHeadWrapper`
- `EditPlaybackContext`

If true, the crash may be:

- a Tracktion bug on macOS
- a Tracktion portability bug in this specific stopped-state path
- or a valid Tracktion contract that we are violating

### Hypothesis 2: our `ValueTree` listener integration is re-entrant in a bad way

Since `134d6a5`, `Engine::Impl` listens directly to Tracktion's transport `ValueTree` for
`tracktion::IDs::position`.

That means a `seek()` can synchronously cause:

1. Tracktion mutates transport position
2. Tracktion notifies `ValueTree` listeners
3. our `Engine::Impl::valueTreePropertyChanged(...)` runs inside that sequence
4. our code reads transport state and may trigger further transport-related behavior

If this is the problem, then the bug is more likely in our integration than in Tracktion itself.

## Things To Isolate Later

Do not assume `playInStopEnabled` is the root cause without isolating variables first.

The clean follow-up is to test these cases separately:

1. Keep our `ValueTree` listener path, but disable `playInStopEnabled`.
2. Keep `playInStopEnabled`, but temporarily remove or bypass our `ValueTree` listener path.
3. Reproduce with the smallest possible concrete Tracktion edit outside the Rock Hero adapter if
   needed.

That will tell us whether the real problem is:

- Tracktion stopped-state transport behavior,
- our direct `ValueTree` callback integration,
- or the interaction between both.

## Important Constraint

Do not treat a workaround such as:

- `m_edit->playInStopEnabled = false`

as the root-cause fix unless the isolation above is completed first.

That may still be a valid product decision for the editor, but it should not be mistaken for proof
about why the macOS crash happened.

## Files To Revisit

- `libs/rock-hero-audio/src/engine.cpp`
- `libs/rock-hero-audio/include/rock_hero/audio/engine.h`
- `libs/rock-hero-audio/tests/test_engine.cpp`
- `external/tracktion_engine/modules/tracktion_engine/playback/tracktion_TransportControl.cpp`
- `external/tracktion_engine/modules/tracktion_engine/playback/tracktion_TransportControl.h`
- `external/tracktion_engine/modules/tracktion_engine/model/edit/tracktion_Edit.h`

## Practical Next Step

When revisiting this, start by comparing current `engine.cpp` against:

- `134d6a5`
- `e21f699`
- the last known good commit before `134d6a5`

and verify exactly which callback path is present in the crashing build before making more
refactor-stage changes around transport or cursor ownership.
