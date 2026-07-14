/*!
\file audio_device_settings_controller.h
\brief Editor controller for the audio-device settings workflow.
*/

#pragma once

#include <functional>
#include <memory>
#include <rock_hero/common/audio/device/audio_device_settings.h>
#include <rock_hero/common/audio/shared/scoped_listener.h>
#include <rock_hero/editor/core/audio_device/audio_device_settings_view_state.h>
#include <rock_hero/editor/core/audio_device/i_audio_device_settings_controller.h>
#include <rock_hero/editor/core/audio_device/i_audio_device_settings_view.h>

namespace rock_hero::editor::core
{

/*!
\brief Callable used by the controller to dispatch audio-device operations behind a host paint
       fence.

The callable accepts the blocking operation plus a continuation that the host invokes after its
busy indicator has cleared. Used by both OK and Cancel because both reopen the audio device, which
blocks the message thread. When no dispatcher is supplied, the controller falls back to running the
operation synchronously inside the matching intent handler. The async path is what the editor uses
to present its busy overlay before running blocking device-manager calls.
*/
using AudioDeviceSettingsDispatcher =
    std::function<void(std::function<void()> work, std::function<void()> after_cleared)>;

/*! \brief Headless editor workflow controller for one audio-device settings window. */
class AudioDeviceSettingsController final : public IAudioDeviceSettingsController,
                                            private common::audio::IAudioDeviceSettings::Listener
{
public:
    /*!
    \brief Creates the controller around the shared settings backend.
    \param settings Shared audio-device settings workflow; must outlive this controller.
    \param dispatcher Optional hook used by OK and Cancel to run device-manager operations
           behind a host paint fence. When empty, OK and Cancel run synchronously inside the
           matching intent handler.
    */
    explicit AudioDeviceSettingsController(
        common::audio::IAudioDeviceSettings& settings,
        AudioDeviceSettingsDispatcher dispatcher = {});

    /*! \brief Cancels an unfinished settings edit before detaching from the backend. */
    ~AudioDeviceSettingsController() override;

    /*! \brief Copying is disabled because listener registration is owned. */
    AudioDeviceSettingsController(const AudioDeviceSettingsController&) = delete;

    /*! \brief Copy assignment is disabled because listener registration is owned. */
    AudioDeviceSettingsController& operator=(const AudioDeviceSettingsController&) = delete;

    /*! \brief Moving is disabled because listener identity must remain stable. */
    AudioDeviceSettingsController(AudioDeviceSettingsController&&) = delete;

    /*! \brief Move assignment is disabled because listener identity must remain stable. */
    AudioDeviceSettingsController& operator=(AudioDeviceSettingsController&&) = delete;

    /*!
    \brief Attaches the concrete view and pushes the current settings state.
    \param view View to update until the controller is destroyed.
    */
    void attachView(IAudioDeviceSettingsView& view);

    void onAudioSystemSelected(int choice_id) override;
    void onDeviceSelected(int choice_id) override;
    void onInputDeviceSelected(int choice_id) override;
    void onOutputDeviceSelected(int choice_id) override;
    void onInputChannelSelected(int choice_id) override;
    void onStereoOutputPairSelected(int choice_id) override;
    void onSampleRateSelected(int choice_id) override;
    void onBufferSizeSelected(int choice_id) override;
    void onControlPanelRequested() override;
    void onOkRequested() override;
    void onCommitRequested() override;
    void onCancelRequested() override;

private:
    void onAudioDeviceSettingsChanged() override;

    // Pushes freshly derived view state to the attached view, if any.
    void updateView();

    // Requests the attached view close after marking the edit complete.
    void finishAndClose();

    // Shared settings backend that owns staging policy and hardware-side apply behavior.
    common::audio::IAudioDeviceSettings& m_settings;

    // Optional dispatcher used by OK and Cancel to run device-manager operations behind a host
    // paint fence.
    AudioDeviceSettingsDispatcher m_dispatcher;

    // Non-owning view binding installed by attachView().
    IAudioDeviceSettingsView* m_view{};

    // Most recently pushed state used for button gating.
    AudioDeviceSettingsViewState m_last_state{};

    // True once OK or Cancel has completed, so native close is the only destructor cancel path.
    bool m_finished{false};

    // Reset during destruction so deferred apply callbacks can detect that the controller is gone.
    std::shared_ptr<bool> m_alive{std::make_shared<bool>(true)};

    // Declared last so listener deregistration happens before referenced state is destroyed.
    common::audio::ScopedListener<
        common::audio::IAudioDeviceSettings, common::audio::IAudioDeviceSettings::Listener>
        m_settings_listener;
};

} // namespace rock_hero::editor::core
