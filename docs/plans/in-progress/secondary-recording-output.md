# Secondary Recording Output

Status: proposed. Written 2026-09-26. Recheck the audio engine and device-settings code before
implementation; this plan records the intended behavior, not a completed design decision.

## Goal

Let Rock Hero keep the Quad Cortex ASIO device for low-latency guitar input and monitoring while
sending the finished stereo mix to an optional second system output, such as VB-Cable's
`CABLE Input` on Windows. OBS records the cable's `CABLE Output` endpoint. The Editor gets the first
settings UI; the shared audio engine owns the routing so the Game can use it later.

## Implementation

1. **Tap the completed mix once.** In `rock-hero-common/audio`, take a copy after Tracktion has
   mixed backing playback and processed guitar, including the master gain. Inspect Tracktion's
   `DeviceManager::setGlobalOutputAudioProcessor` as the candidate hook. The tap must leave the
   primary ASIO output unchanged and do no allocation, locking, or device work in its callback.
2. **Bridge the independent clocks.** Copy stereo samples into a preallocated single-producer,
   single-consumer FIFO. An output-only JUCE device callback drains it. Support differing sample
   rates and adjust the read rate gently around a target fill level so clock drift cannot cause a
   long recording to underrun or overflow. On underflow emit silence; on overflow discard capture
   samples without delaying the primary callback. Use JUCE's `AbstractFifo` and a variable-ratio
   interpolator (`LagrangeInterpolator` or `WindowedSincInterpolator`); only the fill-level
   controller is project-written, and it is headless so fake clocks can test it.
3. **Manage the secondary device independently.** Enumerate and open outputs of the platform's
   shared-mode device type (WASAPI shared on Windows, CoreAudio on macOS) through a JUCE
   `AudioIODeviceType` owned apart from the primary `juce::AudioDeviceManager`, so no OS guard is
   needed. Enable, disable, disconnect, and reopen the recording route without rebuilding the live
   guitar path or changing the primary ASIO device. Report an unavailable recording output
   separately; live playback must continue.
4. **Report the capture delay.** The recording feed trails what the player hears by the FIFO's
   target fill plus the secondary device's output latency, while the captured screen follows the
   primary clock. Show that fixed delay in the recording-output status so the user can enter it once
   as the OBS source's Sync Offset.
5. **Expose and persist the choice.** Add an optional Recording Output selector and status to the
   Editor audio settings. Persist the selection as its own field on `IAudioConfigStore`, separate
   from the primary device route, so each app's store holds one; the editor's
   `EditorAudioConfigStore` routes it through `active()` exactly like the primary route, so it
   follows the game settings whenever those are in use. Never silently substitute another output if
   the saved recording device is unavailable.
6. **Verify.** Test that backing audio and monitored guitar both reach the secondary output, master
   gain affects both routes, and disabling or losing the secondary device leaves ASIO playback
   intact. Exercise sample-rate mismatch, callback-size variation, underrun/overflow, and sustained
   clock drift with headless fakes. Finish with an OBS recording using Audio Input Capture on
   `CABLE Output`; check stereo, that the reported delay as Sync Offset aligns audio and video, and
   a long playback session.

## Alternatives Considered

ASIO sends audio straight to the driver, so nothing Windows-side can observe it: OBS's Application
Audio Capture and WASAPI loopback of the Quad Cortex endpoint both see only the Windows mixer, not
ASIO. The Quad Cortex has no loopback, and the obs-asio plugin lists input channels only.

- **Voicemeeter virtual ASIO.** Rock Hero would open Voicemeeter's virtual ASIO driver, and
  Voicemeeter would own the Quad Cortex. That works, but it puts a second buffering stage on both
  the guitar input and the monitored output — the path this feature must leave untouched.
- **ASIO Link Pro or Synchronous Audio Router.** Both are multi-client ASIO routers. Both are
  unmaintained third-party drivers, and SAR needs test-signing on current Windows.
- **Physical loopback.** Patch a Quad Cortex output into one of its inputs and capture that input
  with obs-asio. That needs spare analog I/O, a second analog conversion, and care to keep the
  looped input out of the monitored mix. It also depends on the QC driver accepting two ASIO clients
  at once, which Neural DSP does not document.

The in-app route keeps the live path unchanged and needs only a free virtual cable, so it stands.

## Constraints

- The secondary output is a capture feed, not a second timing authority. Transport and visual
  synchronization continue to follow the primary ASIO audio clock.
- Opening or configuring the secondary device may block on the message thread, never on an audio
  callback. Its output callback must perform bounded work without allocation or locks.
- OBS monitoring of the cable should stay off to avoid hearing a delayed duplicate of the primary
  ASIO output.
