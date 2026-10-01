# Safe Automatic Device Reopen

Status: deferred (user, 2026-09-30). The editor and the game do not reopen a device that comes back
after an unplug; the user reopens it from the audio settings. This plan records why, what a safe
reopen would look like, and the direction the user chose for later: find out why reopening is
unsafe inside JUCE and fix it there. JUCE ships inside our own `tnt-coders/tracktion_engine` fork
(`external/tracktion_engine/`), so a framework patch is on the table.

Re-verify every citation below against the current code before acting on it. They were taken on
2026-09-30.

## History

- Until 2026-07-14 the engine reopened the saved device on its own (`savedDeviceIsPresent` and
  `enforceNoFallbackDevicePolicy`, last seen at `2f9fce7d`, deleted by `74f43fa5`). Users saw
  crashes with flaky ASIO drivers. There is no crash dump, so the cause was never proven.
- Since 2026-10-01 `enforceDevicePolicy` (`engine_device_config.cpp`) keeps a device running:
  the saved hardware, else a silent device. It never reopens hardware. Losing the hardware pauses
  playback once, and a game session reports `AudioDeviceClosed`.

## Why the old reopen was unsafe

Two mechanisms match the code. Either one may have caused the crashes.

1. **A driver probe at USB-enumeration time.** To test presence, the old code called
   `createDevice`. On ASIO that loads the driver and runs `IASIO::init()`
   (`juce_ASIO_windows.cpp` `openDevice` → `loadDriver` / `initDriver`). JUCE guards
   `CoCreateInstance` and `Release` with SEH, but not `init()`, so a driver fault there kills the
   process. WASAPI and DirectSound each run their own device-change detector, so one physical plug
   produced several refreshes, and each one loaded the driver while Windows was still enumerating
   the USB device. JUCE also keeps an unplugged ASIO device current, because the ASIO name list
   comes from the registry and still lists it. The probe could therefore build a second instance of
   a driver that was already loaded.
2. **Reopening inside the refresh pass.** The old reopen called `initialise()` in the middle of
   the configuration refresh. Tracktion's wave-device rebuild from its own change callback was still
   pending, and `dispatchPendingUpdates()` then forced `prepareToStart` against a device opened
   moments earlier in the same stack. This is crash hypothesis H3 in the backlog.

## Per-backend facts

| Backend | Does a closed device hear a plug? | Does the name list show presence without a probe? |
|---|---|---|
| WASAPI (all three modes) | Yes: `IMMNotificationClient` and `WM_DEVICECHANGE`, 500 ms debounce, rescan, notify on change | Yes: only `DEVICE_STATE_ACTIVE` endpoints are listed |
| DirectSound | Yes | Yes |
| CoreAudio | Yes, through the hardware property listener | Yes |
| ASIO | No: the type notifies only from an open device's reset timer | No: the names come from `HKLM\software\asio` and do not change on hot-plug |
| ALSA | No listener | No: `scanForDevices` caches after its first scan |

## A safe reopen within today's JUCE

- **Trigger.** Reopen when the saved device's name goes from absent to present in its type's
  `getDeviceNames()`, never because a device is closed and still listed. Store the baseline with
  the saved identity it describes. Seed it on the first observation, and reseed it whenever the
  identity changes, in both cases without firing.
- **No probing, no forced scans.** Never call `createDevice` to test presence, and never call
  `scanForDevices`. Each type rescans before it notifies.
- **Timing.** Run the reopen on its own turn after the refresh pass, with
  `juce::Timer::callAfterDelay` guarded by `m_alive`. Re-check everything before opening: a saved
  route still exists, the device is still closed and still listed, and no settings session is
  staging.
- **One path.** Open through the same no-fallback restore that an explicit apply uses, by moving
  the body of `restoreSerializedDeviceState` into one Impl method.
- **Settings window.** Staging closes the device directly through JUCE, so the engine cannot see
  it. A scoped staging token on `IAudioDeviceConfiguration`, held for the whole session, must
  suppress the reopen.
- **No loops.** A failed reopen leaves the device listed, so there is no new edge and no retry.
- **Coverage.** This version covers WASAPI, DirectSound and CoreAudio. The edge never fires for
  ASIO, with no special case, because its list never changes. ALSA gets nothing.

## The direction chosen: fix it in JUCE

The user's preference is to remove the unsafety at its source rather than route around it. A
source-level analysis of the vendored JUCE (2026-09-30; `juce_ASIO_windows.cpp` matches upstream on
every path below) found:

- **JUCE's own ASIO reset path is defective, with no RockHero code involved.** When a driver asks
  for a reset (typically on unplug), `timerCallback` closes and reopens the device. The reopen
  ignores `loadDriver()`'s result and then dereferences a null `asioObject` in `getSampleRate()`.
  When `init()` fails it logs and carries on driving an uninitialised driver. Afterwards it calls
  `reloadChannelNames()` and `start(oldCallback)` even when the reopen failed. JUCE forum threads
  report crashes at exactly these sites (Focusrite, Yamaha/Steinberg USB).
- **The old RockHero probe made it worse.** After a failed reset the dead device object stays
  current, still holding its driver. The `createDevice` probe then loaded a *second* instance of
  the same driver, and the probe fully opens the driver (`init`, `createBuffers`, `start`, an
  80 ms sleep, `stop`). The old reopen also reused that stale device object instead of creating a
  fresh one.
- **ASIO has no hot-plug model.** Its name list comes from the registry, and no OS datum maps an
  ASIO driver to a hardware node. A presence test without loading the driver is impossible, apart
  from name heuristics that fail for wrappers such as ASIO4ALL.
- **Tracktion needs no change.** Its suspend/`prepareToStart` model makes a reopen safe at any
  message-thread point, provided the reopen runs on its own turn after the refresh pass.

The patch set, in order:

1. **P0 (JUCE, upstreamable).** Make the reset path honest. When `loadDriver` or `init` fails,
   release the driver and return the error, and in `timerCallback` start only after a successful
   reopen. Bound the channel counts `reloadChannelNames` trusts.
2. **RockHero: close a dead device.** Done 2026-10-01 as a side effect of the silent device:
   `enforceDevicePolicy` replaces any device that is not the saved hardware with the silent one,
   which destroys the stale object and its driver instance before any later open.
3. **P1 (JUCE).** SEH-guard `IASIO::init` and the first calls `openDevice` makes into the driver,
   through a helper beside `tryCreatingDriver`. On a caught fault, set an error and treat that
   driver as poisoned until restart. SEH only contains a fault on the calling thread. It cannot
   contain one on a driver-owned thread, and it does not undo heap damage the driver did first.
   Out-of-process probing is the only full containment.
4. **P2 (JUCE, fork-only).** Give the ASIO device type a `DeviceChangeDetector`, so a hardware
   change at least *signals* ASIO. That makes the probe rare, one attempt per arrival, rather than
   impossible.
5. **RockHero reopen on top.** Reopen only on an edge: the saved name going from absent to
   present for WASAPI, DirectSound and CoreAudio, or the hardware-arrival signal for ASIO, after a
   2-3 s settle delay. Make at most one attempt per edge, on its own message-thread turn, and never
   while the settings window is staging. Always call `closeAudioDevice()` before
   `initialise(...)`. The attempt *is* the probe: no `createDevice`, no `scanForDevices`.

Experiments come first, because each fix is a guess until one dump exists:

- Build `relwithdebinfo` with `JUCE_ASIO_DEBUGGING=1`.
- Capture dumps through WER `LocalDumps` (`DumpType=2`) or `procdump -ma -e`.
- With today's no-reopen build, play on ASIO, unplug, read the log, and replug. Repeat with the
  settings window open, and plug/unplug five times quickly.
- The dump's faulting thread decides whether P1 can contain the fault: a fault in the driver on
  the message thread can be contained; one on a driver-owned thread or in heap code cannot.

Open uncertainties: which drivers send `kAsioResetRequest` on unplug; whether the July crashes
came from JUCE's reset path or from the probe's double driver instance (only a dump tells); whether
a faulting driver leaves locks that make the process unusable after a caught fault; how often a
first WASAPI open fails on real interfaces; and whether an endpoint keeps its name in a different
USB port.

## The editor's device-lost notice

Since 2026-10-01 the editor raises `AudioDeviceLostPrompt` ("Audio device not running", with Audio
Settings and Close) when a running device is lost, and at a startup whose saved device did not open.
A reopen must retire it: the controller clears the prompt on its closed-to-open observation
(`onAudioDeviceConfigurationChanged`), and the view, which today can only present a themed question
box, needs a way to close one whose prompt is gone. The user asked for this (2026-10-01): a notice
that clears when the device returns, provided the reopen itself is not buggy or a workaround.

## Manual hardware checklist (for whichever version ships)

1. WASAPI shared, exclusive and low-latency, each with a USB interface. Unplug during playback: the
   device closes, the transport pauses, and nothing falls back. Replug: exactly one reopen attempt,
   and Play comes back.
2. Start with the interface unplugged, then plug it in.
3. Replug while another app holds the device exclusively: one failed attempt, then quiet.
4. Plug and unplug quickly five times: no crash, at most one attempt per appearance.
5. Settings window open during an unplug and replug: no reopen under the window.
6. ASIO interfaces: no crash, and record what the status shows while unplugged.
7. CoreAudio, including an aggregate device.
8. Game: an unplug pauses the session, and a reopen does not resume it.
9. Measure how long the reopen blocks the message thread (relwithdebinfo).
