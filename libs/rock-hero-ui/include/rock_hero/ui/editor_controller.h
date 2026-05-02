/*!
\file editor_controller.h
\brief Headless editor workflow coordinator backed by core, transport, and edit ports.
*/

#pragma once

#include <optional>
#include <rock_hero/audio/i_edit.h>
#include <rock_hero/audio/i_transport.h>
#include <rock_hero/audio/scoped_listener.h>
#include <rock_hero/core/audio_asset.h>
#include <rock_hero/core/session.h>
#include <rock_hero/core/track.h>
#include <rock_hero/ui/editor_view_state.h>
#include <rock_hero/ui/i_editor_controller.h>
#include <rock_hero/ui/i_editor_view.h>
#include <string>

namespace rock_hero::ui
{

/*!
\brief Concrete editor workflow coordinator.

Translates editor user intents into transport, edit, and session updates without exposing JUCE
types. Subscribes to the transport listener surface for coarse transition-shaped updates and
re-derives EditorViewState whenever a real change has occurred. The controller owns
load-error policy, play/pause/stop gating, and seek normalization. Continuous playhead motion is
not the controller's responsibility; the editor view pulls position from ITransport::position()
at its own render cadence. The controller samples position only for discrete workflow gates such
as whether Stop can reset the cursor. It provides only discrete cursor mapping state, such as
visible timeline range, through EditorViewState.

The referenced session, transport, and edit ports must outlive the controller.
*/
class EditorController final : public IEditorController, private audio::ITransport::Listener
{
public:
    /*!
    \brief Builds the controller, subscribes to transport, and captures initial view state.

    The controller does not push state during construction because no view is attached yet. The
    initial cached state becomes the first push delivered to a view passed to attachView().

    \param session Session whose tracks back the editor view projections.
    \param transport Transport port used for play/pause/stop/seek and coarse listener delivery.
    \param edit Edit port used to load audio assets for session tracks.
    */
    EditorController(core::Session& session, audio::ITransport& transport, audio::IEdit& edit);

    /*! \brief Releases the transport listener registration before owned references go away. */
    ~EditorController() override;

    /*! \brief Copying is disabled because the controller owns a transport listener registration. */
    EditorController(const EditorController&) = delete;

    /*!
    \brief Copy assignment is disabled because the controller owns a transport listener
    registration.
    */
    EditorController& operator=(const EditorController&) = delete;

    /*! \brief Moving is disabled so the listener registration stays bound to one controller. */
    EditorController(EditorController&&) = delete;

    /*!
    \brief Move assignment is disabled so the listener registration stays bound to one controller.
    */
    EditorController& operator=(EditorController&&) = delete;

    /*!
    \brief Binds a view non-owningly and pushes the cached editor state once.

    Subsequent transport transitions and intent results push fresh state through the same view.
    Calling attachView() with a different view replaces the binding; the previous view stops
    receiving updates.

    \param view View to receive future state pushes.
    */
    void attachView(IEditorView& view);

    /*!
    \brief Handles a request to assign an audio asset to one track.

    The request is ignored when the track id does not exist in the session. On a successful edit,
    the controller commits the accepted clip to the session and clears any active load error. On a
    failed edit, the session is preserved and a controller-composed error is published. Reentrant
    transport notifications received during the edit are coalesced into a single final post-load
    push.

    \param track_id Track whose audio clip should change.
    \param audio_asset Framework-free audio asset selected by the user.
    */
    void onLoadAudioAssetRequested(core::TrackId track_id, core::AudioAsset audio_asset) override;

    /*!
    \brief Handles a play/pause button press from the editor UI.

    The intent is ignored when no session track has an audio clip. Otherwise, plays or pauses
    based on the current transport state.
    */
    void onPlayPausePressed() override;

    /*!
    \brief Handles a stop button press from the editor UI.

    The intent is ignored when the transport is not currently playing and is already at the start
    of the loaded timeline, mirroring the published EditorViewState.stop_enabled value.
    */
    void onStopPressed() override;

    /*!
    \brief Handles a click on a waveform at a normalized horizontal position.

    The input is clamped to [0, 1] and converted through the session timeline range. A duration
    of zero results in a seek to the start of the timeline.

    \param normalized_x Click position normalized to the interval [0, 1].
    */
    void onWaveformClicked(double normalized_x) override;

private:
    // Transport listener entry point; receives only coarse transition-shaped callbacks.
    void onTransportStateChanged(audio::TransportState state) override;

    // Builds a fresh EditorViewState from the current session and transport state.
    [[nodiscard]] EditorViewState deriveViewState() const;

    // Derives a fresh state, caches it, and pushes it to the attached view if any.
    void deriveAndPush();

    // Reports whether at least one session track currently has an audio clip assigned.
    [[nodiscard]] bool anyTrackHasClip() const;

    // Reports whether Stop would either stop playback or reset a non-start cursor position.
    [[nodiscard]] bool canStopTransport(const audio::TransportState& transport_state) const;

    // Session whose tracks drive view projection and load validation.
    core::Session& m_session;

    // Transport port used for control intents and coarse listener delivery.
    audio::ITransport& m_transport;

    // Edit port used to load audio assets before committing accepted clips to the session.
    audio::IEdit& m_edit;

    // Non-owning view binding installed by attachView(); null before the first attachment.
    IEditorView* m_view{nullptr};

    // Most recently derived view state used as the seed push at view attachment.
    EditorViewState m_last_state{};

    // Persisted controller-composed load error preserved across unrelated state transitions.
    std::optional<std::string> m_last_load_error{};

    // Set true while an IEdit call is in flight so reentrant transport callbacks defer pushing.
    bool m_edit_in_progress{false};

    // Records that a transport callback arrived during an in-flight edit so the controller
    // produces exactly one final post-edit push instead of one stale and one final push.
    bool m_pending_refresh{false};

    // Declared last so transport callbacks are detached before controller state is destroyed.
    audio::ScopedListener<audio::ITransport, audio::ITransport::Listener> m_transport_listener;
};

} // namespace rock_hero::ui
