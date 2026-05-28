# Audio Device Settings Performance Investigation

Status: deferred investigation. The first device-switch improvement landed; keep this as a future
measurement plan if real device changes still feel too slow.

## Context

Audio-device changes now feel noticeably faster after pre-closing the active device before a JUCE
audio-system switch. That avoids JUCE's fixed 1.5 second sleep in `setCurrentAudioDeviceType()`
when a current device is open.

Real device changes can still take roughly a second. That is enough to justify the current busy
presentation, but it is still slow for a low-latency app and much slower than REAPER appears to be
on comparable device changes.

## Why Investigate

Rock Hero depends on low-latency audio setup feeling direct and trustworthy. If device changes are
slow because of avoidable framework behavior, the project should know that before hardening the
settings workflow or game-facing audio configuration.

## Current Observations

- `AudioDeviceSettings::apply()` is now faster for cross-system changes because it closes the
  active device before calling `juce::AudioDeviceManager::setCurrentAudioDeviceType()`.
- JUCE still performs real backend work during device creation and apply.
- WASAPI `createDevice()` initializes endpoint objects and probes capabilities before returning.
- ASIO device construction opens the driver and performs capability checks immediately.
- Same-backend changes still go through `setAudioDeviceSetup()`, which can stop, recreate, open,
  and start the current device.

These are initial source observations, not a full timing breakdown.

## Investigation Plan

1. Add temporary or debug-only timing around `AudioDeviceSettings::apply()`:
   - pre-close active device
   - `setCurrentAudioDeviceType()`
   - `setAudioDeviceSetup()`
   - rollback, when applicable
   - final `refreshState()`
2. Add timings around settings-window refresh hotspots:
   - `scanForDevices()`
   - preview `createDevice()`
   - sample-rate queries
   - buffer-size queries
   - channel-name queries
3. Measure real hardware paths:
   - ASIO to WASAPI shared
   - WASAPI shared to low-latency mode
   - WASAPI shared to exclusive mode
   - same backend with sample-rate or buffer-size changes
   - failed apply and rollback
4. Compare measured hotspots with JUCE internals before deciding on further changes.
5. If the bottleneck is capability probing, consider caching capabilities by audio system and
   device route.
6. If the bottleneck is backend open/start, evaluate whether a local JUCE patch, a narrower
   device-switch path, or a future project-owned Windows audio backend is justified.

## Success Criteria

- The team can explain where the remaining roughly one second is spent.
- The busy overlay remains only if real apply paths still visibly block.
- Any optimization preserves rollback behavior and avoids interrupting audio merely by opening or
  cancelling the settings window.
