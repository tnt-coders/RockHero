#include "controller/editor_controller_impl.h"
#include "shared/editor_controller_logging.h"

#include <expected>
#include <functional>
#include <optional>
#include <string>
#include <utility>

namespace rock_hero::editor::core
{

namespace
{

// Maps a non-adoptable game-audio source state onto its typed error carrying the canonical
// user-facing message every reporting surface shares.
[[nodiscard]] GameAudioSourceError gameAudioSourceErrorFor(GameAudioSourceState state)
{
    return GameAudioSourceError{
        state == GameAudioSourceState::Uncalibrated ? GameAudioSourceErrorCode::Uncalibrated
                                                    : GameAudioSourceErrorCode::NotConfigured
    };
}

// Reason shown by the failure prompt when the engine has no more specific diagnostic to offer.
constexpr const char* g_generic_device_failure_reason{"The audio device could not be opened."};

} // namespace

// Wraps the supplied audio-device open work in the editor's busy overlay paint fence so the
// blocking presentation paints once before juce::AudioDeviceManager occupies the message thread.
// The settings dialog launcher provides work already aware of dialog success/failure handling, so
// this method owns only the busy lifecycle.
void EditorController::Impl::onAudioDeviceChangeRequested(
    std::function<void()> change_audio_device, std::function<void()> after_busy_cleared)
{
    if (!change_audio_device || m_live_input_monitor.promptVisible())
    {
        if (after_busy_cleared)
        {
            after_busy_cleared();
        }
        return;
    }

    m_busy.runMessageThreadBusyOperation(
        BusyOperation::OpeningAudioDevice,
        std::move(change_audio_device),
        std::move(after_busy_cleared));
}

// Persists the new device manager state and re-derives view state after a configuration change.
// This is where a mid-session disconnect (the engine's no-fallback policy closing JUCE's
// substitute) surfaces, so the failure-prompt evaluation runs on every configuration change.
void EditorController::Impl::onAudioDeviceConfigurationChanged()
{
    persistAudioDeviceState();
    static_cast<void>(m_live_input_monitor.refresh(monitoringContext()));
    refreshAudioDeviceFailurePrompt();
    updateView();
}

// Marks the audio settings window active so route transitions can be committed as one change.
// Refuses while the calibration prompt is up so the two modal flows never overlap.
bool EditorController::Impl::onAudioDeviceSettingsOpenRequested()
{
    if (m_live_input_monitor.promptVisible())
    {
        return false;
    }

    if (m_transport.state().playing)
    {
        m_transport.pause();
    }

    m_live_input_monitor.openAudioDeviceSettings();
    refreshAudioDeviceFailurePrompt();
    updateView();
    return true;
}

// Re-applies the route gate after settings closes or restores its previous route. The failure
// prompt is deliberately NOT evaluated here: on the native-close path the staged edit's cancel
// backstop runs one message hop later (inside the view's window-reset callAsync), so the device
// can be transiently closed at this moment. onAudioDeviceSettingsTeardownComplete() evaluates
// once the teardown has settled.
void EditorController::Impl::onAudioDeviceSettingsClosed()
{
    static_cast<void>(m_live_input_monitor.closeAudioDeviceSettings(monitoringContext()));
    updateView();
}

// Runs after the settings window object is fully torn down: any staged-edit rollback (including
// the native-close cancel backstop) has settled the device synchronously by now, so this is the
// first trustworthy moment to evaluate whether the editor ended up without an open device.
void EditorController::Impl::onAudioDeviceSettingsTeardownComplete()
{
    refreshAudioDeviceFailurePrompt();
    updateView();
}

// Applies the user's answer to the audio-device failure prompt. Retry re-applies the active
// source's saved route; the no-op applying presentation routes the genuine reopen through the
// busy overlay, and the busy-clear evaluation re-stages the prompt with a fresh generation when
// the device is still closed. OpenSettings clears the prompt only: the view follows by opening
// the settings window, whose suppression owns the prompt until teardown. ExitEditor runs the
// regular exit flow (unsaved-changes prompting included) -- the escape hatch for a user with no
// working audio device at all.
void EditorController::Impl::onAudioDeviceFailureDecision(AudioDeviceFailureDecision decision)
{
    m_audio_device_failure_prompt.reset();

    switch (decision)
    {
        case AudioDeviceFailureDecision::Retry:
        {
            static_cast<void>(applyAudioSourceAndRoute(AudioSourceSelection::Current, [](bool) {}));
            return;
        }
        case AudioDeviceFailureDecision::OpenSettings:
        {
            break;
        }
        case AudioDeviceFailureDecision::ExitEditor:
        {
            onExitRequested();
            return;
        }
    }

    updateView();
}

// The one evaluation deciding whether the audio-device failure prompt should be staged.
// Stage-if-absent: repeat device events never re-bump a staged prompt (no modal flicker), while
// the Retry and settings flows clear the member first so their re-evaluation stages a fresh
// generation that the view's present-once tracking re-presents.
void EditorController::Impl::refreshAudioDeviceFailurePrompt()
{
    const common::audio::AudioDeviceStatus status = m_audio_devices.currentDeviceStatus();
    if (status.open)
    {
        m_audio_device_failure_prompt.reset();
        return;
    }

    // A missing saved route means there is nothing to retry (a fresh install before any device
    // was chosen, or a composition without a device backend); the settings window is the
    // resolution path and the standing "[audio device closed]" status covers presentation.
    const std::optional<common::audio::ActiveDeviceRoute> route =
        m_audio_config_store.activeDeviceRoute();
    if (!route.has_value() || route->serialized_state.empty())
    {
        m_audio_device_failure_prompt.reset();
        return;
    }

    // The settings window deliberately stages with the device closed; the prompt resumes at
    // teardown when the window leaves the device closed for real.
    if (m_live_input_monitor.audioDeviceSettingsOpen())
    {
        m_audio_device_failure_prompt.reset();
        return;
    }

    // A device operation is mid-flight (staged apply, toggle flip, Retry itself); the busy
    // workflow's state callback re-evaluates once it clears, so nothing can flash under the busy
    // overlay.
    if (isBusy())
    {
        return;
    }

    // Startup precedence: the plan-48 game-audio prompts resolve first (never two modals at
    // once); their decision handlers and the settings teardown re-evaluate afterwards.
    if (m_game_audio_unavailable_prompt.has_value() || m_game_audio_recommendation_prompt)
    {
        return;
    }

    if (m_audio_device_failure_prompt.has_value())
    {
        return;
    }

    m_audio_device_failure_prompt = AudioDeviceFailurePrompt{
        .message = !status.unavailable_reason.empty()
                       ? status.unavailable_reason
                       : std::string{g_generic_device_failure_reason},
        .device_name = status.unavailable_device_name,
        .generation = ++m_audio_device_failure_generation,
    };
}

// Applies a "use game audio settings" toggle change through the shared application path. This is
// the toggle side-effect the plan 48 "Separate State From Side Effects" split keeps in the
// controller: the source flip is pure state on the store; adopting the game's (on) or restoring
// the editor's own (off) serialized device state is the message-thread side effect.
std::expected<void, GameAudioSourceError> EditorController::Impl::
    onUseGameAudioSettingsChangeRequested(bool enabled, std::function<void(bool)> set_applying)
{
    return applyAudioSourceAndRoute(
        enabled ? AudioSourceSelection::Game : AudioSourceSelection::EditorOwn,
        std::move(set_applying));
}

// The one route-application path shared by startup, the settings-window toggle, the startup
// recommendation decision, and the failure prompt's Retry.
std::expected<void, GameAudioSourceError> EditorController::Impl::applyAudioSourceAndRoute(
    AudioSourceSelection selection, std::function<void(bool)> set_applying)
{
    if (selection == AudioSourceSelection::Game)
    {
        // Enabling is gated on a fresh read of the game's configuration: when it is not adoptable
        // the request is declined with the typed reason and nothing is persisted or flipped, so a
        // persisted on always means adoption succeeded. While the game source is active its store
        // view is read-only, so any device-route persist attempt fails loudly rather than
        // silently landing in the editor's own store.
        const GameAudioSourceState source_state = gameAudioSourceState();
        if (source_state != GameAudioSourceState::Available)
        {
            return std::unexpected{gameAudioSourceErrorFor(source_state)};
        }
    }

    if (selection != AudioSourceSelection::Current)
    {
        const bool use_game_source = selection == AudioSourceSelection::Game;
        recordSettingsResultBestEffort(
            m_settings.setUseGameAudioSettings(use_game_source),
            "persist use-game-audio-settings toggle");
        if (m_editor_audio_config_store != nullptr)
        {
            m_editor_audio_config_store->useGameSource(use_game_source);
        }
    }

    // Decide instant-vs-overlay before touching the device, using the now-active route. When the
    // resolved route already matches the open device (e.g. the game route was imported from this
    // editor's own route), restoreAudioDeviceState reads through the store and no-ops, so apply
    // it inline with no overlay. Only a genuine device re-open goes behind the busy overlay so
    // the blocking juce::AudioDeviceManager work paints "Opening audio device..." first.
    //
    // The monitoring source can flip regardless of whether the device changed, so the live-input
    // monitor refresh runs on both paths, outside the device-reapply skip.
    const std::optional<common::audio::ActiveDeviceRoute> route =
        m_audio_config_store.activeDeviceRoute();
    const bool device_reopen_required =
        route.has_value() && !route->serialized_state.empty() &&
        !m_audio_devices.deviceStateMatchesActive(route->serialized_state);

    if (device_reopen_required && set_applying)
    {
        // Reuse the OK/Cancel apply presentation: the dialog hides itself for the duration of
        // the blocking re-open (set_applying true, then false once the overlay clears) while
        // the editor's busy overlay paints "Opening audio device..." in its place. The failure
        // prompt is evaluated by the busy workflow's state callback once the overlay clears.
        set_applying(true);
        m_busy.runMessageThreadBusyOperation(
            BusyOperation::OpeningAudioDevice,
            [this] {
                restoreAudioDeviceState();
                static_cast<void>(m_live_input_monitor.refresh(monitoringContext()));
            },
            [set_applying] { set_applying(false); });
        return {};
    }

    // Same-device applications run instantly. A required re-open with no applying presentation
    // (startup, and the cancel-time toggle restore whose window is already closing) also runs
    // inline: routing the cancel-time restore through the busy workflow would let the cancel's
    // own staged-device rollback supersede its token and drop the re-open.
    restoreAudioDeviceState();
    static_cast<void>(m_live_input_monitor.refresh(monitoringContext()));
    refreshAudioDeviceFailurePrompt();
    updateView();
    return {};
}

// Fresh one-shot read of the game's adoption-readiness, queried by the settings window at open so
// it can disable the toggle when no game configuration exists. Degenerate no-store compositions
// (tests without the toggle wired) read as not configured, matching the toggle handler's decline.
GameAudioSourceState EditorController::Impl::gameAudioSourceState() const
{
    return m_editor_audio_config_store != nullptr ? m_editor_audio_config_store->gameSourceState()
                                                  : GameAudioSourceState::NotConfigured;
}

// Clears the startup unavailable-game notice once the view has shown it. The view follows the
// dismissal by opening the audio device settings window, landing the user directly in the editable
// editor-own flow the fallback selected (the settings-open suppression owns the failure prompt
// from there).
void EditorController::Impl::onGameAudioUnavailablePromptDismissed()
{
    m_game_audio_unavailable_prompt.reset();
    updateView();
}

// Applies the user's answer to the startup game-audio recommendation prompt. The suppression
// checkbox is honored on every close path — including Dismissed — because suppressing future
// recommendations is meaningful regardless of today's answer.
void EditorController::Impl::onGameAudioRecommendationDecision(
    GameAudioRecommendationDecision decision, bool suppress_future)
{
    m_game_audio_recommendation_prompt = false;
    if (suppress_future)
    {
        recordSettingsResultBestEffort(
            m_settings.setSuppressGameAudioRecommendation(true),
            "persist game-audio recommendation suppression");
    }

    switch (decision)
    {
        case GameAudioRecommendationDecision::UseGameSettings:
        {
            // Same attempt logic as the settings-window checkbox. The prompt is only staged when
            // adoption can succeed, but the game's file may have changed while the prompt was up;
            // a mid-prompt regression falls back to the unavailable-game notice rather than
            // failing silently. The no-op applying presentation routes a genuine device re-open
            // through the busy overlay so the blocking open paints "Opening audio device..."
            // first.
            std::expected<void, GameAudioSourceError> adopted =
                applyAudioSourceAndRoute(AudioSourceSelection::Game, [](bool) {});
            if (!adopted.has_value())
            {
                m_game_audio_unavailable_prompt =
                    GameAudioUnavailablePrompt{std::move(adopted.error())};
            }
            break;
        }
        case GameAudioRecommendationDecision::UseCustomSettings:
        {
            // The same application path as every other source decision: persists the toggle off
            // and re-applies the editor's own route (a no-op when it is already active). The view
            // has already opened the audio device settings window for this decision ("Open
            // Settings"), so the settings-open suppression keeps the failure-prompt evaluation
            // below quiet until that window tears down.
            static_cast<void>(applyAudioSourceAndRoute(AudioSourceSelection::EditorOwn, {}));
            break;
        }
        case GameAudioRecommendationDecision::Dismissed:
        {
            break;
        }
    }

    // The recommendation prompt was the failure prompt's startup suppressor; with a decision
    // landed, a deferred closed-device notice can surface now.
    refreshAudioDeviceFailurePrompt();
    updateView();
}

// Resolves the "use game audio settings" toggle at startup, before the constructor's route
// application reads through the store (plan 48 amended ruleset). Every branch works off a fresh
// read of the game's file at this moment; at most one startup prompt can be staged because the
// branches are keyed on the toggle. Decision layer only: the route side effect runs through the
// constructor's applyAudioSourceAndRoute call.
void EditorController::Impl::resolveGameAudioSourceAtStartup()
{
    if (m_editor_audio_config_store == nullptr)
    {
        return;
    }

    if (useGameAudioSettingsOrDefault(m_settings))
    {
        const GameAudioSourceState source_state = m_editor_audio_config_store->gameSourceState();
        if (source_state == GameAudioSourceState::Available)
        {
            // Select the game source now so the constructor's route application adopts the
            // game's route.
            m_editor_audio_config_store->useGameSource(true);
            return;
        }

        // The game's configuration regressed after a successful adoption (a persisted on is only
        // ever written when adoption succeeded). Fail loudly once and fall back: write the toggle
        // off, stay on the editor's own route, and stage the notice the view presents and follows
        // with the device settings window.
        recordSettingsResultBestEffort(
            m_settings.setUseGameAudioSettings(false), "persist use-game-audio-settings toggle");
        m_game_audio_unavailable_prompt =
            GameAudioUnavailablePrompt{gameAudioSourceErrorFor(source_state)};
        return;
    }

    // Suppression is checked before the availability read so a suppressed recommendation never
    // reopens the game's settings file at startup.
    if (m_settings.suppressGameAudioRecommendation().value_or(false))
    {
        return;
    }

    if (m_editor_audio_config_store->gameSourceState() == GameAudioSourceState::Available)
    {
        m_game_audio_recommendation_prompt = true;
    }
}

// Applies the active device route stored by a previous editor session, if any. The route's resolved
// identity is not needed to reopen the device; only the opaque blob feeds the device manager.
void EditorController::Impl::restoreAudioDeviceState()
{
    const std::optional<common::audio::ActiveDeviceRoute> route =
        m_audio_config_store.activeDeviceRoute();
    if (!route.has_value() || route->serialized_state.empty())
    {
        return;
    }

    const auto restored = m_audio_devices.restoreSerializedDeviceState(route->serialized_state);
    if (!restored.has_value())
    {
        logEditorControllerBestEffortFailure(
            "restore serialized audio-device state", restored.error().message);
        recordAudioConfigResultBestEffort(
            m_audio_config_store.setActiveDeviceRoute(std::nullopt),
            "clear invalid serialized audio-device state");
        return;
    }

    if (*restored == common::audio::DeviceRestoreOutcome::DeviceUnavailable)
    {
        // A designed outcome, not a failure: the saved device is absent or in use, so the route was
        // applied closed and the saved choice retained. The failure-prompt evaluation surfaces the
        // recorded reason; logged so a headless run is explainable too.
        logEditorControllerBestEffortFailure(
            "open saved audio device",
            "saved device unavailable; the audio device stays closed and the saved choice is kept");
    }
}

// Stores the current device blob paired with its resolved input identity so the next launch can
// restore the user's selection and answer availability questions offline.
void EditorController::Impl::persistAudioDeviceState()
{
    std::optional<std::string> serialized_state = m_audio_devices.serializedDeviceState();
    if (serialized_state.has_value() && !serialized_state->empty())
    {
        recordAudioConfigResultBestEffort(
            m_audio_config_store.setActiveDeviceRoute(
                common::audio::ActiveDeviceRoute{
                    .serialized_state = std::move(*serialized_state),
                    .identity = m_audio_devices.currentInputDeviceIdentity(),
                }),
            "persist serialized audio-device state");
    }
    else
    {
        recordAudioConfigResultBestEffort(
            m_audio_config_store.setActiveDeviceRoute(std::nullopt),
            "clear serialized audio-device state");
    }
}

} // namespace rock_hero::editor::core
