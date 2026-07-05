/*!
\file tone_document.h
\brief Tone-document model plus read/write and plugin-state persistence helpers.
*/

#pragma once

#include "plugin_scan.h"

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

// One persisted plugin entry in the default linear live rig chain.
struct PluginRecord
{
    std::string id;
    PluginIdentity identity;
    std::string tracktion_state_ref;

    // Opaque editor-owned visual block carried through the tone document. The audio layer never
    // interprets it (no gap rules, no validation); playback ignores it entirely. The editor owns
    // its meaning and validity.
    std::size_t block_index{};

    // Opaque editor-owned display type override token carried through the tone document. Empty
    // means the editor should use its automatic display classification.
    std::string display_type_override;
};

// V1 tone document subset currently used by the linear plugin-chain runtime.
struct ToneDocument
{
    std::vector<PluginRecord> chain;
    Gain output_gain;
};

// Resolves a package-relative file and verifies it exists inside the song workspace.
[[nodiscard]] std::optional<std::filesystem::path> resolvePackageFile(
    const std::filesystem::path& song_directory, const std::string& relative_path);

// Builds the generated package-relative tone document path for a new tone.
[[nodiscard]] std::filesystem::path generatedToneDocumentPath();

// Derives the stable state-file namespace owned by a tone document path.
[[nodiscard]] std::filesystem::path toneDocumentStateDirectory(
    const std::filesystem::path& tone_document_ref);

// Builds one package-relative Tracktion plugin-state sidecar path.
[[nodiscard]] std::filesystem::path generatedPluginStatePath(
    const std::filesystem::path& state_directory, std::size_t plugin_index);

// Reports whether a Tracktion state sidecar stays in the tone document's co-located state folder.
[[nodiscard]] bool isCanonicalPluginStateRef(
    const std::string& plugin_state_ref, const std::filesystem::path& expected_state_directory);

// Creates directories needed for a package-relative output file.
[[nodiscard]] std::expected<void, LiveRigError> createParentDirectory(
    const std::filesystem::path& output_file);

// Writes a UTF-8 text file after creating its parent directories.
[[nodiscard]] std::expected<void, LiveRigError> writeTextFile(
    const std::filesystem::path& path, const std::string& contents,
    LiveRigErrorCode write_error_code);

// Serializes identity to the tone document's JSON shape.
[[nodiscard]] juce::var makeIdentityJson(const PluginIdentity& identity);

// Serializes the v1 tone document subset used by the current linear chain.
[[nodiscard]] juce::var makeToneDocumentJson(const ToneDocument& document);

// Reads the v1 tone document subset and validates all package-relative sidecar paths.
[[nodiscard]] std::expected<ToneDocument, LiveRigError> readToneDocument(
    const std::filesystem::path& song_directory, const std::string& tone_document_ref);

// Writes the v1 tone document JSON file.
[[nodiscard]] std::expected<void, LiveRigError> writeToneDocument(
    const std::filesystem::path& tone_document_path, const ToneDocument& document);

// Reads a Tracktion plugin-state sidecar into a ValueTree.
[[nodiscard]] std::expected<juce::ValueTree, LiveRigError> readPluginStateTree(
    const std::filesystem::path& plugin_state_path);

// Serializes a Tracktion plugin ValueTree exactly enough for Tracktion to recreate the plugin.
[[nodiscard]] std::expected<std::string, LiveRigError> makePluginStateXml(
    const juce::ValueTree& plugin_state, const std::filesystem::path& plugin_state_path);

// Serializes a live plugin ValueTree into the in-memory memento form used by editor undo.
[[nodiscard]] std::expected<PluginInstanceState, PluginHostError> makePluginInstanceState(
    const juce::ValueTree& plugin_state);

// Converts a Tracktion plugin into editor-facing runtime chain state.
[[nodiscard]] PluginChainEntry makePluginChainEntry(
    const tracktion::Plugin& plugin, std::size_t chain_index);

} // namespace rock_hero::common::audio
