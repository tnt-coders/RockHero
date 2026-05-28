# Editor Core Framework Isolation Plan

## Purpose

`EditorController` in `rock-hero-editor/core` is headless workflow code. The architectural
principles say core modules should remain automated-testable without windows, audio devices, or the
full application shell, and that framework integration belongs in thin adapters.

Two categories of JUCE framework usage currently live inside `EditorController::Impl` rather than
behind project-owned ports or in the app/UI tier:

1. Audio device state serialization — `restoreAudioDeviceState()` and `persistAudioDeviceState()`
   parse JUCE XML and call through `IAudioDeviceConfiguration::deviceManager()` to manipulate the
   raw `juce::AudioDeviceManager`.
2. Message-thread scheduling — `makeBusyAudioAnalyzeForGainFunction()` and
   `startLiveRigLoadStage()` use `juce::MessageManager` and `juce::Timer::callAfterDelay` for
   cross-thread dispatch and cosmetic timing.

This plan addresses both. Part 1 should be implemented now. Part 2 should be deferred unless a
concrete need arises.

## Part 1: Audio Device State Serialization

### Problem

`restoreAudioDeviceState()` (editor_controller.cpp line 3147) reads an XML string from
`EditorSettings`, parses it with `juce::parseXML` and `juce::String`, then calls
`m_audio_devices->deviceManager().initialise(1, 2, xml.get(), true)`.

`persistAudioDeviceState()` (line 3171) calls
`m_audio_devices->deviceManager().createStateXml()`, converts the result to `std::string`, and
writes it to `EditorSettings`.

This pulls `juce_core` XML parsing and `juce_audio_devices` device-manager mutation into
`editor/core`. The controller reaches past `IAudioDeviceConfiguration` to touch the raw
`juce::AudioDeviceManager` for serialization, even though the port already abstracts device status
and input identity.

The `deviceManager()` accessor exists so `editor/ui` can hand JUCE's built-in
`AudioDeviceSelectorComponent` its manager — that is a legitimate UI-tier use. But `editor/core`
should not need it for serialization.

### Change

Add two methods to `IAudioDeviceConfiguration`:

```cpp
virtual void restoreState(const std::string& serialized_state) = 0;
[[nodiscard]] virtual std::optional<std::string> saveState() const = 0;
```

`restoreState` replaces `EditorController::Impl::restoreAudioDeviceState()`. The production adapter
in `Engine` handles XML parsing and calls `deviceManager().initialise()` internally.

`saveState` replaces `EditorController::Impl::persistAudioDeviceState()`. The production adapter
calls `deviceManager().createStateXml()` and converts to `std::string` internally.

`EditorController::Impl` then simplifies to:

```cpp
void EditorController::Impl::restoreAudioDeviceState()
{
    if (m_audio_devices == nullptr || m_settings == nullptr)
    {
        return;
    }

    const std::optional<std::string> state = m_settings->audioDeviceState();
    if (state.has_value() && !state->empty())
    {
        m_audio_devices->restoreState(*state);
    }
}

void EditorController::Impl::persistAudioDeviceState()
{
    if (m_audio_devices == nullptr || m_settings == nullptr)
    {
        return;
    }

    m_settings->setAudioDeviceState(m_audio_devices->saveState());
}
```

### Steps

1. Add `restoreState` and `saveState` to `IAudioDeviceConfiguration`.
2. Implement both in `Engine` by moving the XML parsing and `deviceManager()` calls from the
   controller into the adapter. Invalid XML in `restoreState` should be silently ignored (matching
   the current controller behavior that discards unparseable XML without reporting an error).
3. Update the `FakeAudioDeviceConfiguration` in `test_editor_controller.cpp` to implement the new
   methods. The fake can store the string and return it, or no-op — the test fake for audio-device
   settings in `test_audio_device_settings.cpp` needs the same update.
4. Replace the bodies of `restoreAudioDeviceState()` and `persistAudioDeviceState()` in
   `EditorController::Impl` with the simplified versions above.
5. Remove the `juce_audio_devices` and `juce_core` XML-related includes from
   `editor_controller.cpp` if they are no longer needed after the change. The remaining JUCE
   includes in the file are `juce_events` (for `MessageManager` and `Timer`, addressed by Part 2)
   and `juce_core` for `juce::String` in `restoreAudioDeviceState` — both should be removable after
   Part 1 if Part 2 is also done, but only the `juce_audio_devices` include and the `juce::parseXML`
   / `juce::XmlElement` / `juce::String` usage should disappear from Part 1 alone.
6. Verify that `editor/core` no longer references `deviceManager()`. The accessor stays on the port
   for `editor/ui` use.

### Test impact

Existing characterization tests for audio-device restore and persist go through
`EditorController`'s public API. The test fake's `restoreState` / `saveState` record whether they
were called and with what argument. The behavior under test does not change — only the serialization
boundary moves.

The `FakeAudioDeviceConfiguration` in `test_editor_controller.cpp` currently returns a real
`juce::AudioDeviceManager` from `deviceManager()`. After this change, the controller no longer
calls `deviceManager()`, but the fake still needs to provide one for the interface contract. No test
should break; the new methods are additive.

## Part 2: Message-Thread Scheduling (Deferred)

### Problem

Three sites in `EditorController::Impl` use JUCE message-loop primitives directly:

1. `makeBusyAudioAnalyzeForGainFunction()` (line 1443) — calls
   `juce::MessageManager::getInstanceWithoutCreating()`, `isThisTheMessageThread()`, and
   `juce::MessageManager::callAsync()` to post a busy-state transition from the worker thread to
   the message thread, then blocks the worker on `AnalysisPaintGate` until the message thread has
   painted.

2. `startLiveRigLoadStage()` (line 1552) — calls `juce::Timer::callAfterDelay` for a 500ms
   cosmetic hold so the 100% live-rig progress state is visible before the busy overlay clears.

3. `startLiveRigLoadStage()` (line 1543) — branches on
   `juce::MessageManager::getInstanceWithoutCreating() == nullptr` to detect the test environment
   and skip the timer-based delay.

These are framework integration in headless workflow code. The test-environment branch (site 3) is
the most architecturally awkward: it encodes "are we in a test?" as "is the JUCE message loop
alive?" inside core logic.

### Why Defer

The fix is more invasive than Part 1 and the practical benefit is lower:

- `JuceEditorTaskRunner` already lives in `editor/core` and uses `juce::MessageManager::callAsync`
  for completion marshaling. It is a JUCE-aware type by design — it exists so the controller does
  not need to schedule completions itself. Extending the task runner (or adding a sibling scheduler
  port) to cover the two remaining callAsync/callAfterDelay sites is straightforward in concept but
  touches async choreography that is currently correct and well-tested.

- The analysis paint gate in `makeBusyAudioAnalyzeForGainFunction` bridges a worker thread to the
  message thread with a condition-variable rendezvous. The worker blocks until the message thread
  has painted the busy overlay. This pattern is tightly coupled to the controller's busy-token
  lifecycle and the view's paint-fence contract. Abstracting the message-thread post behind a port
  does not remove the coupling — it adds an indirection layer over it.

- The test-environment branch works correctly today. The inline task runner runs work synchronously,
  so the `MessageManager` is never involved in tests. The branch is inelegant but not a source of
  bugs.

### When To Revisit

Implement Part 2 when one of these is true:

- A second consumer in `editor/core` needs to post work to the message thread outside the task
  runner, making a shared message-thread dispatch port genuinely reusable rather than single-purpose.
- The test-environment branch becomes a source of bugs or false test passes because the
  synchronous test path and the async production path diverge in a way that matters.
- A new platform or runtime environment (such as a headless CI test host) needs to replace JUCE's
  message loop but cannot because `editor/core` calls it directly.

### Shape If Implemented

If Part 2 is implemented, the likely approach is:

1. Add a `postToMessageThread(std::function<void()>)` method to `IEditorTaskRunner` (or introduce a
   narrower `IMessageThreadScheduler` port if the task runner's submit/complete contract should not
   grow). Also add `callAfterDelay(std::chrono::milliseconds, std::function<void()>)` for the
   cosmetic timer case.

2. The production `JuceEditorTaskRunner` (or a sibling scheduler) implements these through
   `juce::MessageManager::callAsync` and `juce::Timer::callAfterDelay`.

3. The inline test runner implements `postToMessageThread` by calling the function synchronously,
   and `callAfterDelay` by calling the function immediately (skipping the delay). This eliminates
   the `MessageManager == nullptr` branch from the controller.

4. Replace the three direct JUCE calls in `EditorController::Impl` with calls through the port.

5. Remove the `juce_events` include from `editor_controller.cpp`.

This would make `editor_controller.cpp` entirely free of direct JUCE includes, completing the
framework isolation of the controller's workflow logic.