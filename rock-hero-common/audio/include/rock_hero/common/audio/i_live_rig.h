/*!
\file i_live_rig.h
\brief Tracktion-free live guitar rig port.
*/

#pragma once

#include <cstddef>
#include <expected>
#include <filesystem>
#include <functional>
#include <rock_hero/common/audio/gain.h>
#include <rock_hero/common/audio/live_rig_error.h>
#include <string>
#include <vector>

namespace rock_hero::common::audio
{

/*! \brief One plugin restored into or captured from the active live rig chain. */
struct [[nodiscard]] LiveRigPlugin
{
    /*! \brief Opaque instance ID assigned by the current plugin backend. */
    std::string instance_id;

    /*! \brief Best-effort plugin identifier for display and same-runtime operations. */
    std::string plugin_id;

    /*! \brief User-facing plugin name. */
    std::string name;

    /*! \brief User-facing plugin manufacturer, when supplied by the scanner. */
    std::string manufacturer;

    /*! \brief Backend plugin format name, such as VST3. */
    std::string format_name;

    /*! \brief Zero-based position in the current linear plugin chain. */
    std::size_t chain_index{};
};

/*! \brief Message-thread request to capture the active live rig into song files. */
struct [[nodiscard]] LiveRigCaptureRequest
{
    /*! \brief Native song workspace directory that owns package-relative tone files. */
    std::filesystem::path song_directory;

    /*! \brief Canonical package arrangement ID whose rig is being captured. */
    std::string arrangement_id;

    /*! \brief Existing package-relative tone document path, when one should be overwritten. */
    std::string existing_tone_document_ref;
};

/*! \brief Result of writing the active live rig into song tone files. */
struct [[nodiscard]] LiveRigSnapshot
{
    /*! \brief Package-relative tone document path to store on the arrangement. */
    std::string tone_document_ref;

    /*! \brief Captured chain state for the editor signal-chain panel. */
    std::vector<LiveRigPlugin> plugins;

    /*! \brief Captured output gain after the signal chain. */
    Gain output_gain;
};

/*! \brief Progress reported while restoring plugins into the live rig. */
struct [[nodiscard]] LiveRigLoadProgress
{
    /*! \brief Number of plugins fully restored into the live rig so far. */
    std::size_t completed_plugins{};

    /*! \brief Total plugin count expected for the current tone document. */
    std::size_t total_plugins{};

    /*! \brief Zero-based index of the active plugin name within the current tone document. */
    std::size_t active_plugin_index{};

    /*! \brief User-facing name of the plugin currently being restored, when known. */
    std::string active_plugin_name;
};

/*! \brief Callback used by live rig restore to report determinate plugin progress. */
using LiveRigLoadProgressCallback = std::function<void(const LiveRigLoadProgress&)>;

/*!
\brief Callback used by live rig restore to yield to the message loop between plugin steps.

The implementation should arrange for `next` to run after the message loop has had a chance to
process pending paints; this is what makes per-plugin progress updates actually visible to the
user. The editor wires this to a paint-fence helper that waits for the busy overlay to repaint
before resuming work. When unset, the engine falls back to a plain async post that does not
guarantee a paint cycle between steps.
*/
using LiveRigLoadYieldCallback = std::function<void(std::function<void()> next)>;

/*! \brief Message-thread request to restore a tone document into the live rig. */
struct [[nodiscard]] LiveRigLoadRequest
{
    /*! \brief Native song workspace directory that owns package-relative tone files. */
    std::filesystem::path song_directory;

    /*! \brief Package-relative tone document path stored on the arrangement. */
    std::string tone_document_ref;

    /*! \brief Optional callback invoked as plugin restore progress changes. */
    LiveRigLoadProgressCallback progress_callback;

    /*!
    \brief Optional callback used to yield to the message loop between plugin steps.

    When set, the engine calls this between each cooperative step and waits for the supplied
    continuation to be invoked before running the next step. Wire this to a paint-fence helper so
    pending paints actually run between steps; otherwise the engine falls back to plain async
    posts and per-step progress updates may not be visible.
    */
    LiveRigLoadYieldCallback yield_callback;
};

/*! \brief Result of loading a tone document into the live rig chain. */
struct [[nodiscard]] LiveRigLoadResult
{
    /*! \brief Restored chain state for the editor signal-chain panel. */
    std::vector<LiveRigPlugin> plugins;

    /*! \brief Restored output gain after the signal chain. */
    Gain output_gain;
};

/*!
\brief Callback invoked on the message thread once an async live rig load has fully finished.

Fires exactly once per loadLiveRig() call, after every plugin in the chain has been restored or
after the operation fails. Per-plugin updates during the load are delivered through
LiveRigLoadProgressCallback instead.
*/
using LiveRigLoadResultCallback =
    std::function<void(std::expected<LiveRigLoadResult, LiveRigError>)>;

/*!
\brief Project-owned facade for the currently loaded playable guitar rig.

All methods are message-thread operations. Implementations may stop transport, scan plugin files,
load plugin state, and rebuild backend playback graphs; callers must never invoke this interface
from the real-time audio callback.
*/
class ILiveRig
{
public:
    /*! \brief Destroys the live rig interface. */
    virtual ~ILiveRig() = default;

    /*!
    \brief Captures the active live rig chain into package-relative song files.
    \param request Song workspace and arrangement identity for the capture.
    \return Written tone document reference and display chain, or a typed failure.
    */
    [[nodiscard]] virtual std::expected<LiveRigSnapshot, LiveRigError> captureActiveRig(
        const LiveRigCaptureRequest& request) = 0;

    /*!
    \brief Loads a package-relative tone document into the active live rig chain.

    The operation runs cooperatively on the message thread: each plugin is restored in its own
    message-loop turn so the message loop can service paints and input between plugins. The
    completion callback fires on the message thread with the restored chain or a typed failure.
    For an empty tone document reference the completion fires immediately with an empty result.

    \param request Song workspace, tone document reference, and optional progress callback.
    \param completion Callback invoked once the operation finishes or fails.
    */
    virtual void loadLiveRig(LiveRigLoadRequest request, LiveRigLoadResultCallback completion) = 0;

    /*!
    \brief Clears the active live rig chain.
    \return Empty success, or a typed failure.
    */
    [[nodiscard]] virtual std::expected<void, LiveRigError> clearLiveRig() = 0;

    /*!
    \brief Reads the current output gain applied after the signal chain.
    \return Current output gain, or the default when no structural gain plugin exists.
    */
    [[nodiscard]] virtual Gain outputGain() const = 0;

    /*!
    \brief Sets the output gain applied after the signal chain.
    \param gain Desired output gain; clamped to the accepted range.
    \return Empty success, or a typed failure.
    */
    [[nodiscard]] virtual std::expected<void, LiveRigError> setOutputGain(Gain gain) = 0;

protected:
    /*! \brief Creates the live rig interface. */
    ILiveRig() = default;

    /*! \brief Copies the live rig interface. */
    ILiveRig(const ILiveRig&) = default;

    /*! \brief Moves the live rig interface. */
    ILiveRig(ILiveRig&&) = default;

    /*!
    \brief Assigns the live rig interface from another interface.
    \return Reference to this live rig interface.
    */
    ILiveRig& operator=(const ILiveRig&) = default;

    /*!
    \brief Move-assigns the live rig interface from another interface.
    \return Reference to this live rig interface.
    */
    ILiveRig& operator=(ILiveRig&&) = default;
};

} // namespace rock_hero::common::audio
