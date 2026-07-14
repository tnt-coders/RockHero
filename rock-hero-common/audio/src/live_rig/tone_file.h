/*!
\file tone_file.h
\brief Standalone .rocktone tone-file container: one portable rig, no automation, no identity.
*/

#pragma once

#include "live_rig/tone_document.h"

#include <expected>
#include <filesystem>
#include <juce_data_structures/juce_data_structures.h>
#include <rock_hero/common/audio/live_rig/live_rig_error.h>
#include <span>
#include <string_view>
#include <vector>

namespace rock_hero::common::audio
{

/*! \brief File extension used by standalone tone files, including the leading dot. */
inline constexpr std::string_view g_tone_file_extension{".rocktone"};

/*! \brief Fully validated in-memory contents of a standalone tone file. */
struct ToneFilePayload
{
    /*!
    \brief Parsed tone document with archive-canonical sidecar refs.

    Stable ids arrive cleared: a tone file never supplies durable plugin identity, so importers
    always mint fresh ids for the plugins they instantiate.
    */
    ToneDocument document;

    /*! \brief Hygiene-stripped plugin state trees, parallel to the document chain. */
    std::vector<juce::ValueTree> plugin_states;
};

/*!
\brief Reads and fully validates a standalone tone file.

Transactional: container structure, document shape, sidecar containment, and every plugin-state
entry are parsed and validated before returning, so a failure never yields a partial payload.
Defensive hygiene re-strips derived automation curves and stale tempo-remap flags from every
state tree and clears file-carried stable ids, so a hand-edited file cannot smuggle either past
the capture-side guarantees.

\param tone_file_path Absolute path of the tone file to read.
\return Validated payload, or the load failure to report.
*/
[[nodiscard]] std::expected<ToneFilePayload, LiveRigError> readToneFile(
    const std::filesystem::path& tone_file_path);

/*!
\brief Writes one rig as a standalone tone file.

The writer owns the container layout: sidecar references are derived from chain position and
stable ids are omitted, so a tone file never carries identity or automation regardless of what
the caller's records hold. State trees are hygiene-stripped copies; the caller's live trees are
never mutated.

\param tone_file_path Absolute output path; parent directories are created when missing.
\param document Tone document to persist; sidecar refs and stable ids are normalized on write.
\param plugin_states Plugin state trees parallel to the document chain.
\return Empty success, or the persistence failure to report.
*/
[[nodiscard]] std::expected<void, LiveRigError> writeToneFile(
    const std::filesystem::path& tone_file_path, const ToneDocument& document,
    std::span<const juce::ValueTree> plugin_states);

} // namespace rock_hero::common::audio
