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
- Today `enforceNoFallbackDevicePolicy` (`engine_device_config.cpp`) only closes a substitute
  device. It never opens one. A closed device pauses the transport, Play is unavailable in the
  editor, and a game session reports `AudioDeviceClosed`.

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

The user's preference is to understand and remove the unsafety at its source rather than route
around it. Questions to answer, in order:

1. **Reproduce.** On real ASIO hardware (RME, Focusrite, Behringer, ASIO4ALL) with
   `JUCE_ASIO_DEBUGGING` on, find which mechanism above actually crashes and capture a dump. Until
   then, every fix below is a guess.
2. **Make the ASIO probe survivable.** Guard `initDriver` / `IASIO::init()` the way `loadDriver`
   is already guarded, so a faulting driver produces an error instead of ending the process.
3. **Give ASIO real presence.** Let an ASIO type report presence without loading the driver. For
   example, it could match the driver's device against the OS device list (SetupAPI or MMDevice)
   and notify its listeners when that changes, so the edge rule covers ASIO like any other backend.
4. **Close an unplugged ASIO device.** Today `AudioDeviceManager::audioDeviceListChanged` keeps it
   current because its name is still listed. Decide whether the manager should close it, so the
   status stops reading open while no callbacks run.
5. **Upstream.** Offer each patch to JUCE, and keep any that is not accepted as a documented
   commit in the fork.

Open uncertainties: whether ASIO drivers send `kAsioResetRequest` on replug; how often a first
WASAPI open fails on real interfaces (exclusive-mode contention, firmware still booting); whether
an endpoint keeps its name in a different USB port; and whether `setAudioDeviceSetup`'s equal-setup
early return can report a closed but current device as opened.

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
