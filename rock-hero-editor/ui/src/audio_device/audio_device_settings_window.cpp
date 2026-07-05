#include "audio_device_settings_window.h"

#include "audio_device/audio_device_settings_view.h"

#include <memory>
#include <rock_hero/common/audio/audio_device_settings.h>
#include <rock_hero/editor/core/audio_device/audio_device_settings_controller.h>
#include <utility>

namespace rock_hero::editor::ui
{

namespace
{

// Owner-managed modal dialog that can be hidden for async apply without destroying its content.
class AudioDeviceSettingsDialogWindow final : public juce::DialogWindow,
                                              private juce::ComponentListener
{
public:
    AudioDeviceSettingsDialogWindow(
        juce::Component* centering_component, AudioDeviceSettingsWindow::ClosedCallback closed)
        : juce::DialogWindow(
              "Audio Device Settings", juce::Colours::darkgrey.darker(0.16F), true, true,
              centering_component != nullptr
                  ? juce::Component::getApproximateScaleFactorForComponent(centering_component)
                  : 1.0F)
        , m_centering_component(centering_component)
        , m_closed(std::move(closed))
    {
        setResizable(true, true);
        setUsingNativeTitleBar(true);
        setAlwaysOnTop(juce::WindowUtils::areThereAnyAlwaysOnTopWindows());
        if (auto* owner = m_centering_component.getComponent())
        {
            owner->addComponentListener(this);
        }
    }

    AudioDeviceSettingsDialogWindow(const AudioDeviceSettingsDialogWindow&) = delete;
    AudioDeviceSettingsDialogWindow& operator=(const AudioDeviceSettingsDialogWindow&) = delete;
    AudioDeviceSettingsDialogWindow(AudioDeviceSettingsDialogWindow&&) = delete;
    AudioDeviceSettingsDialogWindow& operator=(AudioDeviceSettingsDialogWindow&&) = delete;

    // Detaches from the owner component unless the owner itself is already being destroyed.
    ~AudioDeviceSettingsDialogWindow() override
    {
        if (auto* owner = m_centering_component.getComponent())
        {
            owner->removeComponentListener(this);
        }
    }

    // Installs owned content and applies the settings-window resize policy before showing.
    void installContent(std::unique_ptr<juce::Component> content, int content_height)
    {
        setContentOwned(content.release(), true);
        centreAroundComponent(m_centering_component, getWidth(), getHeight());
        setResizeLimits(
            AudioDeviceSettingsView::minimumWidth(),
            content_height,
            AudioDeviceSettingsView::maximumWidth(),
            AudioDeviceSettingsView::maximumHeight());
    }

    // Starts a non-auto-delete modal lifetime; final close notifies the external owner.
    void showModal()
    {
        enterManagedModalState();
    }

    // Hides fully for async apply, then restores modality if the apply fails.
    void setApplying(bool applying)
    {
        if (m_close_requested)
        {
            return;
        }

        if (m_applying == applying)
        {
            return;
        }

        m_applying = applying;
        if (m_applying)
        {
            m_restore_after_temporary_modal_exit = false;
            m_modal_end_is_temporary = true;
            setVisible(false);
            if (m_centering_component != nullptr)
            {
                m_centering_component->toFront(true);
            }
            return;
        }

        if (m_modal_end_is_temporary)
        {
            // setVisible(false) ends JUCE modality asynchronously. Re-entering before that
            // callback retires the old modal item can leave stale modal state behind the window.
            m_restore_after_temporary_modal_exit = true;
            return;
        }

        restoreAfterApplying();
    }

    // Treats OK, Cancel, title-bar close, and Escape as final disposal paths.
    void requestClose()
    {
        if (m_close_requested)
        {
            return;
        }

        m_close_requested = true;
        setVisible(false);
        notifyClosed();
        if (isCurrentlyModal(false))
        {
            exitModalState(0);
        }
    }

    // Routes native title-bar close through the same final disposal path as controller close.
    void closeButtonPressed() override
    {
        requestClose();
    }

    // Keeps Escape from using DialogWindow's default hide-only behavior.
    bool escapeKeyPressed() override
    {
        requestClose();
        return true;
    }

private:
    // The dialog owns settings state that references the audio backend. Ask the external owner
    // to release the window if the component that launched it is being torn down.
    void componentBeingDeleted(juce::Component& component) override
    {
        if (&component != m_centering_component.getComponent())
        {
            return;
        }

        m_centering_component = nullptr;
        requestClose();
    }

    // Sends the final close notification exactly once.
    void notifyClosed()
    {
        if (!m_closed)
        {
            return;
        }

        auto closed = std::move(m_closed);
        closed();
    }

    // Enters JUCE modality without auto-delete and attaches a guarded finalization callback.
    void enterManagedModalState()
    {
        if (isCurrentlyModal(false) || m_close_requested)
        {
            return;
        }

        const juce::Component::SafePointer<AudioDeviceSettingsDialogWindow> safe_this{this};
        enterModalState(
            true,
            juce::ModalCallbackFunction::create([safe_this](int) {
                if (auto* window = safe_this.getComponent())
                {
                    window->handleModalStateFinished();
                }
            }),
            false);
    }

    // Treats only final modal exits as close requests; apply-driven hide/show cycles stay alive.
    void handleModalStateFinished()
    {
        if (m_modal_end_is_temporary && !m_close_requested)
        {
            m_modal_end_is_temporary = false;
            if (m_restore_after_temporary_modal_exit)
            {
                m_restore_after_temporary_modal_exit = false;
                restoreAfterApplying();
            }
            return;
        }

        requestClose();
    }

    // Restores the failed-apply dialog only after JUCE has retired the temporary modal exit.
    void restoreAfterApplying()
    {
        if (m_close_requested)
        {
            return;
        }

        setVisible(true);
        enterManagedModalState();
        toFront(true);
    }

    // Editor top-level component used for centering and for revealing the busy overlay.
    juce::Component::SafePointer<juce::Component> m_centering_component;

    // Callback fired once when the dialog reaches a final close path.
    AudioDeviceSettingsWindow::ClosedCallback m_closed;

    // True while the window is hidden for an in-flight audio-device change.
    bool m_applying{false};

    // Set when the next modal-end callback was caused by apply-driven temporary hiding.
    bool m_modal_end_is_temporary{false};

    // Set when failed apply is ready to restore but JUCE has not fired the temporary modal end yet.
    bool m_restore_after_temporary_modal_exit{false};

    // Set once the workflow has requested final disposal.
    bool m_close_requested{false};
};

// Owns the shared settings service, editor controller, and passive view for one modal window.
class AudioDeviceSettingsWindowContent final : public juce::Component
{
public:
    AudioDeviceSettingsWindowContent(
        common::audio::IAudioDeviceConfiguration& audio_devices,
        core::AudioDeviceSettingsDispatcher dispatcher,
        AudioDeviceSettingsView::ApplyingCallback applying_callback,
        AudioDeviceSettingsView::CloseCallback close_callback)
        : m_settings(audio_devices)
        , m_controller(m_settings, std::move(dispatcher))
        , m_view(m_controller, std::move(applying_callback), std::move(close_callback))
    {
        m_controller.attachView(m_view);
        addAndMakeVisible(m_view);
        setSize(AudioDeviceSettingsView::preferredWidth(), m_view.preferredContentHeight());
    }

    // Keeps the settings view filling the dialog content area.
    void resized() override
    {
        m_view.setBounds(getLocalBounds());
    }

    // Returns the preferred height currently derived by the rendered settings view.
    [[nodiscard]] int preferredContentHeight() const noexcept
    {
        return m_view.preferredContentHeight();
    }

private:
    // Shared backend that owns one staged route edit.
    common::audio::AudioDeviceSettings m_settings;

    // Editor-specific controller that maps settings state into view state.
    core::AudioDeviceSettingsController m_controller;

    // Passive JUCE controls rendered inside this window content.
    AudioDeviceSettingsView m_view;
};

} // namespace

// Launches the audio settings window centered on the editor window that owns the launcher.
std::unique_ptr<juce::DocumentWindow> AudioDeviceSettingsWindow::show(
    common::audio::IAudioDeviceConfiguration& audio_devices, juce::Component& anchor,
    Dispatcher dispatcher, ClosedCallback closed_callback)
{
    juce::Component* const centering_component = anchor.getTopLevelComponent();
    auto window = std::make_unique<AudioDeviceSettingsDialogWindow>(
        centering_component != nullptr ? centering_component : &anchor, std::move(closed_callback));
    const juce::Component::SafePointer<AudioDeviceSettingsDialogWindow> safe_window{window.get()};
    auto content = std::make_unique<AudioDeviceSettingsWindowContent>(
        audio_devices,
        std::move(dispatcher),
        [safe_window](bool applying) {
            if (auto* target_window = safe_window.getComponent())
            {
                target_window->setApplying(applying);
            }
        },
        [safe_window] {
            if (auto* target_window = safe_window.getComponent())
            {
                target_window->requestClose();
            }
        });
    const int content_height = content->preferredContentHeight();

    window->installContent(std::move(content), content_height);
    window->showModal();
    return std::unique_ptr<juce::DocumentWindow>{std::move(window)};
}

} // namespace rock_hero::editor::ui
