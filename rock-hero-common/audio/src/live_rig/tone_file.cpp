#include "tone_file.h"

#include "tracktion/plugin_state_hygiene.h"

#include <memory>
#include <rock_hero/common/core/shared/json.h>
#include <rock_hero/common/core/shared/juce_path.h>
#include <system_error>
#include <utility>

namespace rock_hero::common::audio
{

namespace
{

// Entry name of the tone document at the archive root; the state directory derives from it
// exactly as it does for in-package tone documents, so both containers share one canonical shape.
constexpr std::string_view g_tone_document_entry_name{"tone.json"};

// Mirrors the song-package archive compression so tone files match package density.
constexpr int g_zip_compression_level = 9;

// Returns the canonical in-archive state directory ("state") shared by the writer and reader.
[[nodiscard]] std::filesystem::path archiveStateDirectory()
{
    return toneDocumentStateDirectory(std::filesystem::path{g_tone_document_entry_name});
}

// Adds one in-memory text entry to the archive builder. The builder takes ownership of the
// stream, and MemoryInputStream keeps its own copy of the block, so nothing here depends on
// `contents` outliving the call.
void addTextEntry(
    juce::ZipFile::Builder& builder, const std::string& entry_name, const std::string& contents)
{
    builder.addEntry(
        new juce::MemoryInputStream{juce::MemoryBlock{contents.data(), contents.size()}, true},
        g_zip_compression_level,
        juce::String::fromUTF8(entry_name.c_str()),
        juce::Time{});
}

// Reads one archive entry fully into text.
[[nodiscard]] std::expected<juce::String, LiveRigError> readEntryText(
    juce::ZipFile& archive, int entry_index, const std::string& entry_name)
{
    const std::unique_ptr<juce::InputStream> stream{archive.createStreamForEntry(entry_index)};
    if (stream == nullptr)
    {
        return std::unexpected{LiveRigError{
            LiveRigErrorCode::CouldNotReadToneFile, "Could not open tone file entry: " + entry_name
        }};
    }

    return stream->readEntireStreamAsString();
}

} // namespace

// Reads and fully validates a standalone tone file before anything touches a live chain.
[[nodiscard]] std::expected<ToneFilePayload, LiveRigError> readToneFile(
    const std::filesystem::path& tone_file_path)
{
    std::error_code file_error;
    if (!std::filesystem::is_regular_file(tone_file_path, file_error))
    {
        return std::unexpected{LiveRigError{
            LiveRigErrorCode::CouldNotReadToneFile,
            "Tone file does not exist: " + tone_file_path.string()
        }};
    }

    juce::ZipFile archive{core::juceFileFromPath(tone_file_path)};
    // JUCE reports an unreadable or non-archive file only as an empty entry list, so zero entries
    // is the closest available "this is not a tone file" signal.
    if (archive.getNumEntries() <= 0)
    {
        return std::unexpected{LiveRigError{
            LiveRigErrorCode::InvalidToneFile, "File is not a tone file: " + tone_file_path.string()
        }};
    }

    const std::string document_entry_name{g_tone_document_entry_name};
    const int document_index =
        archive.getIndexOfFileName(juce::String::fromUTF8(document_entry_name.c_str()));
    if (document_index < 0)
    {
        return std::unexpected{LiveRigError{
            LiveRigErrorCode::InvalidToneFile,
            "Tone file has no tone document: " + tone_file_path.string()
        }};
    }

    auto document_text = readEntryText(archive, document_index, document_entry_name);
    if (!document_text.has_value())
    {
        return std::unexpected{std::move(document_text.error())};
    }

    auto parsed_json = core::Json::parseDocument(*document_text);
    if (!parsed_json.has_value())
    {
        return std::unexpected{LiveRigError{
            LiveRigErrorCode::InvalidToneFile,
            "Could not parse tone file document: " + parsed_json.error().message
        }};
    }

    auto document = parseToneDocumentJson(*parsed_json, archiveStateDirectory());
    if (!document.has_value())
    {
        return std::unexpected{std::move(document.error())};
    }

    ToneFilePayload payload;
    payload.document = std::move(*document);
    payload.plugin_states.reserve(payload.document.chain.size());
    for (PluginRecord& record : payload.document.chain)
    {
        const int state_index =
            archive.getIndexOfFileName(juce::String::fromUTF8(record.tracktion_state_ref.c_str()));
        if (state_index < 0)
        {
            return std::unexpected{LiveRigError{
                LiveRigErrorCode::MissingPluginState,
                "Tone file is missing plugin state: " + record.tracktion_state_ref
            }};
        }

        auto xml_text = readEntryText(archive, state_index, record.tracktion_state_ref);
        if (!xml_text.has_value())
        {
            return std::unexpected{std::move(xml_text.error())};
        }

        auto state_tree =
            pluginStateTreeFromXmlText(xml_text->toStdString(), record.tracktion_state_ref);
        if (!state_tree.has_value())
        {
            return std::unexpected{std::move(state_tree.error())};
        }

        // Defensive hygiene: capture already strips these, but a hand-edited file must not be
        // able to smuggle derived automation curves or the tempo-remap flag into a live chain.
        stripAutomationCurves(*state_tree);
        stripTempoRemapFlag(*state_tree);

        // File-carried stable ids are never adopted; importers mint fresh durable ids so
        // arrangement automation can never ambiguously bind across an import.
        record.stable_id.clear();

        payload.plugin_states.push_back(std::move(*state_tree));
    }

    return payload;
}

// Writes one rig as a standalone tone file, owning the container layout end to end.
[[nodiscard]] std::expected<void, LiveRigError> writeToneFile(
    const std::filesystem::path& tone_file_path, const ToneDocument& document,
    std::span<const juce::ValueTree> plugin_states)
{
    if (document.chain.size() != plugin_states.size())
    {
        return std::unexpected{LiveRigError{
            LiveRigErrorCode::InvalidRequest, "Tone file chain and plugin state counts do not match"
        }};
    }

    // Normalize the persisted records: the container owns the sidecar layout, and durable
    // identity keys never travel in a tone file.
    ToneDocument normalized = document;
    const std::filesystem::path state_directory = archiveStateDirectory();
    for (std::size_t index = 0; index < normalized.chain.size(); ++index)
    {
        normalized.chain[index].tracktion_state_ref =
            generatedPluginStatePath(state_directory, index).generic_string();
        normalized.chain[index].stable_id.clear();
    }

    juce::ZipFile::Builder archive_builder;
    const juce::String document_json = juce::JSON::toString(makeToneDocumentJson(normalized));
    addTextEntry(
        archive_builder,
        std::string{g_tone_document_entry_name},
        document_json.toStdString() + '\n');

    for (std::size_t index = 0; index < plugin_states.size(); ++index)
    {
        // Copy before cleaning: the hygiene passes mutate in place and the caller's live trees
        // must stay untouched.
        juce::ValueTree state_copy = plugin_states[index].createCopy();
        stripAutomationCurves(state_copy);
        stripTempoRemapFlag(state_copy);

        const std::string& state_ref = normalized.chain[index].tracktion_state_ref;
        auto xml_text = makePluginStateXml(state_copy, std::filesystem::path{state_ref});
        if (!xml_text.has_value())
        {
            return std::unexpected{std::move(xml_text.error())};
        }

        addTextEntry(archive_builder, state_ref, *xml_text);
    }

    if (!tone_file_path.parent_path().empty())
    {
        if (auto directory_result = createParentDirectory(tone_file_path);
            !directory_result.has_value())
        {
            return std::unexpected{std::move(directory_result.error())};
        }
    }

    juce::FileOutputStream output_stream{core::juceFileFromPath(tone_file_path)};
    if (output_stream.failedToOpen())
    {
        return std::unexpected{LiveRigError{
            LiveRigErrorCode::CouldNotWriteToneFile,
            "Could not open tone file for writing: " +
                output_stream.getStatus().getErrorMessage().toStdString()
        }};
    }

    // FileOutputStream appends to existing files, so rewind and truncate to replace the previous
    // contents; without this an overwrite would corrupt the archive.
    if (!output_stream.setPosition(0))
    {
        return std::unexpected{LiveRigError{
            LiveRigErrorCode::CouldNotWriteToneFile, "Could not seek tone file for writing"
        }};
    }

    const juce::Result truncate_result = output_stream.truncate();
    if (truncate_result.failed())
    {
        return std::unexpected{LiveRigError{
            LiveRigErrorCode::CouldNotWriteToneFile,
            "Could not truncate tone file for writing: " +
                truncate_result.getErrorMessage().toStdString()
        }};
    }

    if (!archive_builder.writeToStream(output_stream, nullptr))
    {
        return std::unexpected{
            LiveRigError{LiveRigErrorCode::CouldNotWriteToneFile, "Could not write tone file"}
        };
    }

    output_stream.flush();
    if (output_stream.getStatus().failed())
    {
        return std::unexpected{LiveRigError{
            LiveRigErrorCode::CouldNotWriteToneFile,
            "Could not flush tone file: " +
                output_stream.getStatus().getErrorMessage().toStdString()
        }};
    }

    return {};
}

} // namespace rock_hero::common::audio
