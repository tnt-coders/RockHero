/*!
\file i_live_rig.h
\brief Tracktion-free live guitar rig port.
*/

#pragma once

#include <cstddef>
#include <expected>
#include <filesystem>
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

    /*! \brief Arrangement ID used to generate stable tone and state sidecar paths. */
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
};

/*! \brief Message-thread request to restore a tone document into the live rig. */
struct [[nodiscard]] LiveRigLoadRequest
{
    /*! \brief Native song workspace directory that owns package-relative tone files. */
    std::filesystem::path song_directory;

    /*! \brief Package-relative tone document path stored on the arrangement. */
    std::string tone_document_ref;
};

/*! \brief Result of loading a tone document into the live rig chain. */
struct [[nodiscard]] LiveRigLoadResult
{
    /*! \brief Restored chain state for the editor signal-chain panel. */
    std::vector<LiveRigPlugin> plugins;
};

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
    \param request Song workspace and package-relative tone document reference.
    \return Restored display chain, or a typed failure.
    */
    [[nodiscard]] virtual std::expected<LiveRigLoadResult, LiveRigError> loadRig(
        const LiveRigLoadRequest& request) = 0;

    /*!
    \brief Clears the active live rig chain.
    \return Empty success, or a typed failure.
    */
    [[nodiscard]] virtual std::expected<void, LiveRigError> clearRig() = 0;

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
