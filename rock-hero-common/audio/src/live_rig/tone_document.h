/*!
\file tone_document.h
\brief Tone-document model plus read/write and plugin-state persistence helpers.
*/

#pragma once

#include "plugin/plugin_scan.h"

#include <expected>
#include <filesystem>
#include <juce_data_structures/juce_data_structures.h>
#include <optional>
#include <rock_hero/common/audio/gain.h>
#include <rock_hero/common/audio/live_rig/i_live_rig.h>
#include <rock_hero/common/audio/plugin/i_plugin_host.h>
#include <string>
#include <tracktion_engine/tracktion_engine.h>
#include <vector>

namespace rock_hero::common::audio
{

/*! \brief One persisted plugin entry in the default linear live rig chain. */
struct PluginRecord
{
    /*! \brief Stable record ID within the tone document. */
    std::string id;

    /*! \brief Durable plugin identity used to resolve the plugin on load. */
    PluginIdentity identity;

    /*! \brief Package-relative Tracktion plugin-state sidecar reference. */
    std::string tracktion_state_ref;

    /*!
    \brief Opaque editor-owned visual block carried through the tone document.

    The audio layer never interprets it (no gap rules, no validation); playback ignores it
    entirely. The editor owns its meaning and validity.
    */
    std::size_t block_index{};

    /*!
    \brief Opaque editor-owned display type override token carried through the document.

    Empty means the editor should use its automatic display classification.
    */
    std::string display_type_override;
};

/*! \brief V1 tone document subset currently used by the linear plugin-chain runtime. */
struct ToneDocument
{
    /*! \brief Persisted plugin chain in playback order. */
    std::vector<PluginRecord> chain;

    /*! \brief Persisted fixed output gain applied after the external chain. */
    Gain output_gain;
};

/*!
\brief Resolves a package-relative file and verifies it exists inside the workspace.
\param song_directory Song workspace root.
\param relative_path Package-relative reference from the tone document.
\return Absolute file path, or empty when the reference is unsafe or missing.
*/
[[nodiscard]] std::optional<std::filesystem::path> resolvePackageFile(
    const std::filesystem::path& song_directory, const std::string& relative_path);

/*!
\brief Builds the generated package-relative tone document path for a new tone.
\return Package-relative tone document path.
*/
[[nodiscard]] std::filesystem::path generatedToneDocumentPath();

/*!
\brief Derives the stable state-file namespace owned by a tone document path.
\param tone_document_ref Package-relative tone document reference.
\return Package-relative state directory co-located with the document.
*/
[[nodiscard]] std::filesystem::path toneDocumentStateDirectory(
    const std::filesystem::path& tone_document_ref);

/*!
\brief Builds one package-relative Tracktion plugin-state sidecar path.
\param state_directory Tone document's package-relative state directory.
\param plugin_index Zero-based chain position of the plugin.
\return Package-relative sidecar path for the plugin's persisted state.
*/
[[nodiscard]] std::filesystem::path generatedPluginStatePath(
    const std::filesystem::path& state_directory, std::size_t plugin_index);

/*!
\brief Reports whether a state sidecar stays in the document's co-located state folder.
\param plugin_state_ref Package-relative sidecar reference from the document.
\param expected_state_directory Canonical state directory for the document.
\return True when the reference resolves inside the expected directory.
*/
[[nodiscard]] bool isCanonicalPluginStateRef(
    const std::string& plugin_state_ref, const std::filesystem::path& expected_state_directory);

/*!
\brief Creates directories needed for a package-relative output file.
\param output_file File whose parent directories should exist afterwards.
\return Empty success, or the persistence failure to report.
*/
[[nodiscard]] std::expected<void, LiveRigError> createParentDirectory(
    const std::filesystem::path& output_file);

/*!
\brief Writes a UTF-8 text file after creating its parent directories.
\param path Output file path.
\param contents UTF-8 file contents.
\param write_error_code Error code to report when the write fails.
\return Empty success, or the persistence failure to report.
*/
[[nodiscard]] std::expected<void, LiveRigError> writeTextFile(
    const std::filesystem::path& path, const std::string& contents,
    LiveRigErrorCode write_error_code);

/*!
\brief Serializes identity to the tone document's JSON shape.
\param identity Persisted plugin identity.
\return Identity JSON object.
*/
[[nodiscard]] juce::var makeIdentityJson(const PluginIdentity& identity);

/*!
\brief Serializes the v1 tone document subset used by the current linear chain.
\param document Tone document to serialize.
\return Tone document JSON object.
*/
[[nodiscard]] juce::var makeToneDocumentJson(const ToneDocument& document);

/*!
\brief Reads the v1 tone document subset and validates all sidecar paths.
\param song_directory Song workspace root.
\param tone_document_ref Package-relative tone document reference.
\return Parsed tone document, or the load failure to report.
*/
[[nodiscard]] std::expected<ToneDocument, LiveRigError> readToneDocument(
    const std::filesystem::path& song_directory, const std::string& tone_document_ref);

/*!
\brief Writes the v1 tone document JSON file.
\param tone_document_path Absolute output path for the document.
\param document Tone document to persist.
\return Empty success, or the persistence failure to report.
*/
[[nodiscard]] std::expected<void, LiveRigError> writeToneDocument(
    const std::filesystem::path& tone_document_path, const ToneDocument& document);

/*!
\brief Reads a Tracktion plugin-state sidecar into a ValueTree.
\param plugin_state_path Absolute sidecar path.
\return Parsed plugin state tree, or the load failure to report.
*/
[[nodiscard]] std::expected<juce::ValueTree, LiveRigError> readPluginStateTree(
    const std::filesystem::path& plugin_state_path);

/*!
\brief Serializes a plugin ValueTree exactly enough for Tracktion to recreate it.
\param plugin_state Tracktion plugin state tree.
\param plugin_state_path Sidecar path used only for failure messages.
\return XML text for the sidecar, or the serialization failure to report.
*/
[[nodiscard]] std::expected<std::string, LiveRigError> makePluginStateXml(
    const juce::ValueTree& plugin_state, const std::filesystem::path& plugin_state_path);

/*!
\brief Serializes a live plugin ValueTree into the memento form used by editor undo.
\param plugin_state Tracktion plugin state tree.
\return Opaque full-state memento, or the capture failure to report.
*/
[[nodiscard]] std::expected<PluginInstanceState, PluginHostError> makePluginInstanceState(
    const juce::ValueTree& plugin_state);

/*!
\brief Converts a Tracktion plugin into editor-facing runtime chain state.
\param plugin Live Tracktion plugin instance.
\param chain_index Zero-based user-visible chain position.
\return Snapshot entry for the plugin.
*/
[[nodiscard]] PluginChainEntry makePluginChainEntry(
    const tracktion::Plugin& plugin, std::size_t chain_index);

} // namespace rock_hero::common::audio
