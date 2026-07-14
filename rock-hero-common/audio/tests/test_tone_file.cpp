#include "live_rig/tone_file.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <fstream>
#include <memory>
#include <rock_hero/common/core/package/package_id.h>
#include <rock_hero/common/core/shared/juce_path.h>
#include <string>
#include <system_error>
#include <tracktion_engine/tracktion_engine.h>
#include <utility>
#include <vector>

namespace rock_hero::common::audio
{

namespace
{

// Creates and removes a unique scratch directory so parallel test runs never clobber each other.
struct ScopedTempDirectory
{
    ScopedTempDirectory()
        : path(
              std::filesystem::temp_directory_path() /
              ("rh_tone_file_test_" + core::generatePackageId()))
    {
        std::filesystem::create_directories(path);
    }

    ~ScopedTempDirectory()
    {
        std::error_code cleanup_error;
        std::filesystem::remove_all(path, cleanup_error);
    }

    ScopedTempDirectory(const ScopedTempDirectory&) = delete;
    ScopedTempDirectory& operator=(const ScopedTempDirectory&) = delete;

    std::filesystem::path path;
};

// Builds a two-plugin document whose records carry every optional field, so tests can assert
// what the container preserves (placement, display type) and what it normalizes away (caller
// sidecar refs, stable ids).
[[nodiscard]] ToneDocument makeTestDocument()
{
    PluginIdentity amp_identity;
    amp_identity.format_name = "VST3";
    amp_identity.name = "Test Amp";
    amp_identity.manufacturer = "RockHero";
    amp_identity.unique_id = "amp-1";

    PluginIdentity cab_identity;
    cab_identity.format_name = "VST3";
    cab_identity.name = "Test Cab";
    cab_identity.manufacturer = "RockHero";
    cab_identity.unique_id = "cab-1";

    ToneDocument document;
    document.chain.push_back(
        PluginRecord{
            .id = "plugin-a",
            .identity = amp_identity,
            .tracktion_state_ref = "caller/supplied/ref.tracktion-plugin",
            .block_index = 2,
            .display_type_override = "pedal",
            .stable_id = "caller-supplied-stable-id",
        });
    document.chain.push_back(
        PluginRecord{
            .id = "plugin-b",
            .identity = cab_identity,
            .tracktion_state_ref = "",
            .block_index = 0,
            .display_type_override = "",
            .stable_id = "",
        });
    document.output_gain = Gain{-3.5};
    return document;
}

// Minimal Tracktion-shaped plugin state with one marker property tests can trace through the
// round trip.
[[nodiscard]] juce::ValueTree makeTestState(const juce::String& marker)
{
    juce::ValueTree state{tracktion::IDs::PLUGIN};
    state.setProperty("testMarker", marker, nullptr);
    state.setProperty(tracktion::IDs::enabled, true, nullptr);
    return state;
}

// Writes an archive with arbitrary text entries so tests can hand-build malformed tone files.
void writeTestArchive(
    const std::filesystem::path& archive_path,
    const std::vector<std::pair<std::string, std::string>>& entries)
{
    juce::ZipFile::Builder builder;
    for (const auto& [entry_name, contents] : entries)
    {
        builder.addEntry(
            new juce::MemoryInputStream{juce::MemoryBlock{contents.data(), contents.size()}, true},
            9,
            juce::String::fromUTF8(entry_name.c_str()),
            juce::Time{});
    }

    juce::FileOutputStream output{core::juceFileFromPath(archive_path)};
    REQUIRE_FALSE(output.failedToOpen());
    REQUIRE(builder.writeToStream(output, nullptr));
}

// Serializes a document through the shared tone-document JSON writer for hand-built archives.
[[nodiscard]] std::string documentJsonText(const ToneDocument& document)
{
    return juce::JSON::toString(makeToneDocumentJson(document)).toStdString();
}

} // namespace

TEST_CASE("Tone file round-trips document records and plugin state", "[audio][tone-file]")
{
    const ScopedTempDirectory temp;
    const std::filesystem::path tone_file = temp.path / "round_trip.tone";

    const ToneDocument document = makeTestDocument();
    const std::vector<juce::ValueTree> states{makeTestState("amp"), makeTestState("cab")};

    const auto write_result = writeToneFile(tone_file, document, states);
    REQUIRE(write_result.has_value());

    const auto payload = readToneFile(tone_file);
    REQUIRE(payload.has_value());
    REQUIRE(payload->document.chain.size() == 2);
    REQUIRE(payload->plugin_states.size() == 2);

    const PluginRecord& first = payload->document.chain[0];
    CHECK(first.id == "plugin-a");
    CHECK(first.identity.name == "Test Amp");
    CHECK(first.identity.format_name == "VST3");
    CHECK(first.identity.unique_id == "amp-1");
    CHECK(first.block_index == 2);
    CHECK(first.display_type_override == "pedal");

    // The writer owns the container layout: caller-supplied refs are replaced with the canonical
    // archive shape, and stable ids never travel in a tone file.
    CHECK(first.tracktion_state_ref == "state/plugin-1.tracktion-plugin");
    CHECK(first.stable_id.empty());
    CHECK(payload->document.chain[1].tracktion_state_ref == "state/plugin-2.tracktion-plugin");
    CHECK(payload->document.chain[1].id == "plugin-b");

    CHECK(payload->document.output_gain.db == Catch::Approx(-3.5));

    CHECK(payload->plugin_states[0].getProperty("testMarker").toString() == "amp");
    CHECK(payload->plugin_states[1].getProperty("testMarker").toString() == "cab");
    // The shared read path strips the live item id exactly like sidecar reads do.
    CHECK_FALSE(payload->plugin_states[0].hasProperty(tracktion::IDs::id));
}

TEST_CASE("Tone file write strips automation curves and tempo remap flags", "[audio][tone-file]")
{
    const ScopedTempDirectory temp;
    const std::filesystem::path tone_file = temp.path / "hygiene.tone";

    ToneDocument document = makeTestDocument();
    document.chain.resize(1);

    juce::ValueTree dirty_state = makeTestState("dirty");
    dirty_state.appendChild(juce::ValueTree{tracktion::IDs::AUTOMATIONCURVE}, nullptr);
    dirty_state.setProperty(tracktion::IDs::remapOnTempoChange, true, nullptr);
    const std::vector<juce::ValueTree> states{dirty_state};

    const auto write_result = writeToneFile(tone_file, document, states);
    REQUIRE(write_result.has_value());

    // The caller's live tree must stay untouched: the writer cleans a copy.
    CHECK(dirty_state.getNumChildren() == 1);
    CHECK(dirty_state.hasProperty(tracktion::IDs::remapOnTempoChange));

    const auto payload = readToneFile(tone_file);
    REQUIRE(payload.has_value());
    REQUIRE(payload->plugin_states.size() == 1);
    CHECK(payload->plugin_states[0].getNumChildren() == 0);
    CHECK_FALSE(payload->plugin_states[0].hasProperty(tracktion::IDs::remapOnTempoChange));
    CHECK(payload->plugin_states[0].getProperty("testMarker").toString() == "dirty");
}

TEST_CASE("Tone file read strips smuggled automation and stable ids", "[audio][tone-file]")
{
    const ScopedTempDirectory temp;
    const std::filesystem::path tone_file = temp.path / "smuggled.tone";

    // Hand-build an archive whose sidecar carries a derived automation curve and whose document
    // carries a stable id — the shapes a hand-edited file could try to smuggle past capture.
    ToneDocument document = makeTestDocument();
    document.chain.resize(1);
    document.chain[0].tracktion_state_ref = "state/plugin-1.tracktion-plugin";
    document.chain[0].stable_id = "smuggled-stable-id";

    juce::ValueTree smuggled_state = makeTestState("smuggled");
    smuggled_state.appendChild(juce::ValueTree{tracktion::IDs::AUTOMATIONCURVE}, nullptr);
    const std::unique_ptr<juce::XmlElement> state_xml = smuggled_state.createXml();
    REQUIRE(state_xml != nullptr);

    writeTestArchive(
        tone_file,
        {
            {"tone.json", documentJsonText(document)},
            {"state/plugin-1.tracktion-plugin", state_xml->toString().toStdString()},
        });

    const auto payload = readToneFile(tone_file);
    REQUIRE(payload.has_value());
    REQUIRE(payload->plugin_states.size() == 1);
    CHECK(payload->plugin_states[0].getNumChildren() == 0);
    CHECK(payload->document.chain[0].stable_id.empty());
}

TEST_CASE("Tone file read fails cleanly for missing and non-archive files", "[audio][tone-file]")
{
    const ScopedTempDirectory temp;

    const auto missing = readToneFile(temp.path / "does_not_exist.tone");
    REQUIRE_FALSE(missing.has_value());
    CHECK(missing.error().code == LiveRigErrorCode::CouldNotReadToneFile);

    const std::filesystem::path junk_file = temp.path / "junk.tone";
    {
        std::ofstream junk{junk_file, std::ios::binary};
        junk << "this is not a zip archive";
    }
    const auto junk_result = readToneFile(junk_file);
    REQUIRE_FALSE(junk_result.has_value());
    CHECK(junk_result.error().code == LiveRigErrorCode::InvalidToneFile);
}

TEST_CASE("Tone file read rejects archives without a tone document", "[audio][tone-file]")
{
    const ScopedTempDirectory temp;
    const std::filesystem::path tone_file = temp.path / "no_document.tone";
    writeTestArchive(tone_file, {{"other.txt", "not a tone document"}});

    const auto payload = readToneFile(tone_file);
    REQUIRE_FALSE(payload.has_value());
    CHECK(payload.error().code == LiveRigErrorCode::InvalidToneFile);
}

TEST_CASE("Tone file read rejects malformed and unsupported documents", "[audio][tone-file]")
{
    const ScopedTempDirectory temp;

    const std::filesystem::path unparsable_file = temp.path / "unparsable.tone";
    writeTestArchive(unparsable_file, {{"tone.json", "this is not json"}});
    const auto unparsable = readToneFile(unparsable_file);
    REQUIRE_FALSE(unparsable.has_value());
    CHECK(unparsable.error().code == LiveRigErrorCode::InvalidToneFile);

    const std::filesystem::path wrong_version_file = temp.path / "wrong_version.tone";
    writeTestArchive(wrong_version_file, {{"tone.json", R"({"formatVersion": 2, "slots": []})"}});
    const auto wrong_version = readToneFile(wrong_version_file);
    REQUIRE_FALSE(wrong_version.has_value());
    CHECK(wrong_version.error().code == LiveRigErrorCode::InvalidToneDocument);
}

TEST_CASE("Tone file read rejects sidecar refs outside the state directory", "[audio][tone-file]")
{
    const ScopedTempDirectory temp;
    const std::filesystem::path tone_file = temp.path / "escaping_ref.tone";

    ToneDocument document = makeTestDocument();
    document.chain.resize(1);
    document.chain[0].tracktion_state_ref = "../escape.tracktion-plugin";

    writeTestArchive(tone_file, {{"tone.json", documentJsonText(document)}});

    const auto payload = readToneFile(tone_file);
    REQUIRE_FALSE(payload.has_value());
    CHECK(payload.error().code == LiveRigErrorCode::InvalidToneDocument);
}

TEST_CASE("Tone file read reports missing plugin state entries", "[audio][tone-file]")
{
    const ScopedTempDirectory temp;
    const std::filesystem::path tone_file = temp.path / "missing_state.tone";

    ToneDocument document = makeTestDocument();
    document.chain.resize(1);
    document.chain[0].tracktion_state_ref = "state/plugin-1.tracktion-plugin";

    writeTestArchive(tone_file, {{"tone.json", documentJsonText(document)}});

    const auto payload = readToneFile(tone_file);
    REQUIRE_FALSE(payload.has_value());
    CHECK(payload.error().code == LiveRigErrorCode::MissingPluginState);
}

TEST_CASE("Tone file write rejects mismatched chain and state counts", "[audio][tone-file]")
{
    const ScopedTempDirectory temp;
    const ToneDocument document = makeTestDocument();
    const std::vector<juce::ValueTree> states{makeTestState("only-one")};

    const auto write_result = writeToneFile(temp.path / "mismatch.tone", document, states);
    REQUIRE_FALSE(write_result.has_value());
    CHECK(write_result.error().code == LiveRigErrorCode::InvalidRequest);
}

} // namespace rock_hero::common::audio
