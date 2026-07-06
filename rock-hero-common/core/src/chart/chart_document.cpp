#include "chart/chart_document.h"

#include <rock_hero/common/core/chart/chart_tokens.h>
#include <rock_hero/common/core/shared/json.h>
#include <rock_hero/common/core/shared/juce_path.h>

namespace rock_hero::common::core
{

namespace
{

[[nodiscard]] ChartError malformed(const std::string& detail)
{
    return ChartError{.code = ChartErrorCode::MalformedDocument, .message = detail};
}

[[nodiscard]] std::expected<GridPosition, ChartError> readPosition(const juce::var& object)
{
    const std::string text = Json::readOptionalString(object, "position", "");
    const auto position = parseGridPositionToken(text);
    if (!position.has_value())
    {
        return std::unexpected{malformed("chart position token is malformed: " + text)};
    }
    return *position;
}

[[nodiscard]] std::expected<Fraction, ChartError> readFraction(
    const juce::var& object, const char* key)
{
    const std::string text = Json::readOptionalString(object, key, "");
    const auto value = parseBeatFractionToken(text);
    if (!value.has_value())
    {
        return std::unexpected{malformed(
            std::string{"chart fraction token is malformed: "} + key + "=" + text)};
    }
    return *value;
}

[[nodiscard]] std::expected<std::vector<std::optional<int>>, ChartError> readOptionalIntArray(
    const juce::var& array_json, const char* context)
{
    std::vector<std::optional<int>> values;
    if (!array_json.isArray())
    {
        return std::unexpected{malformed(
            std::string{"chart template array is missing: "} + context)};
    }
    values.reserve(static_cast<std::size_t>(array_json.size()));
    for (int index = 0; index < array_json.size(); ++index)
    {
        const juce::var& entry = array_json[index];
        if (entry.isVoid())
        {
            values.emplace_back(std::nullopt);
        }
        else if (entry.isInt() || entry.isInt64())
        {
            values.emplace_back(static_cast<int>(entry));
        }
        else
        {
            return std::unexpected{malformed(
                std::string{"chart template entry is not null or int: "} + context)};
        }
    }
    return values;
}

[[nodiscard]] std::expected<ChartNote, ChartError> readNote(const juce::var& note_json)
{
    auto position = readPosition(note_json);
    if (!position.has_value())
    {
        return std::unexpected{std::move(position.error())};
    }

    ChartNote note;
    note.position = *position;
    note.string = Json::readOptionalInt(note_json, "string", 0);
    note.fret = Json::readOptionalInt(note_json, "fret", -1);
    if (Json::value(note_json, "sustain").isString())
    {
        auto sustain = readFraction(note_json, "sustain");
        if (!sustain.has_value())
        {
            return std::unexpected{std::move(sustain.error())};
        }
        note.sustain = *sustain;
    }

    const std::string attack = Json::readOptionalString(note_json, "attack", "");
    if (attack == "hammer")
    {
        note.attack = NoteAttack::Hammer;
    }
    else if (attack == "pull")
    {
        note.attack = NoteAttack::Pull;
    }
    else if (attack == "tap")
    {
        note.attack = NoteAttack::Tap;
    }
    else if (attack == "pop")
    {
        note.attack = NoteAttack::Pop;
    }
    else if (attack == "slap")
    {
        note.attack = NoteAttack::Slap;
    }
    else if (!attack.empty())
    {
        return std::unexpected{malformed("chart note attack is unknown: " + attack)};
    }

    const std::string mute = Json::readOptionalString(note_json, "mute", "");
    if (mute == "palm")
    {
        note.mute = NoteMute::Palm;
    }
    else if (mute == "full")
    {
        note.mute = NoteMute::Full;
    }
    else if (!mute.empty())
    {
        return std::unexpected{malformed("chart note mute is unknown: " + mute)};
    }

    const std::string harmonic = Json::readOptionalString(note_json, "harmonic", "");
    if (harmonic == "natural")
    {
        note.harmonic = NoteHarmonic::Natural;
    }
    else if (harmonic == "pinch")
    {
        note.harmonic = NoteHarmonic::Pinch;
    }
    else if (!harmonic.empty())
    {
        return std::unexpected{malformed("chart note harmonic is unknown: " + harmonic)};
    }

    if (const auto touch = Json::tryReadDouble(note_json, "touch"); touch.has_value())
    {
        note.touch = *touch;
    }

    note.vibrato = Json::readOptionalBool(note_json, "vibrato", false);
    note.tremolo = Json::readOptionalBool(note_json, "tremolo", false);
    note.accent = Json::readOptionalBool(note_json, "accent", false);

    if (const juce::var& bend_json = Json::value(note_json, "bend"); !bend_json.isVoid())
    {
        if (!bend_json.isArray())
        {
            return std::unexpected{malformed("chart note bend must be an array of pairs")};
        }
        note.bend.reserve(static_cast<std::size_t>(bend_json.size()));
        for (int index = 0; index < bend_json.size(); ++index)
        {
            const juce::var& pair = bend_json[index];
            if (!pair.isArray() || pair.size() != 2 || !pair[0].isString())
            {
                return std::unexpected{malformed("chart bend pair must be [offset, semitones]")};
            }
            const auto offset = parseBeatFractionToken(pair[0].toString().toStdString());
            if (!offset.has_value())
            {
                return std::unexpected{malformed("chart bend offset token is malformed")};
            }
            note.bend.push_back(
                BendPoint{.offset = *offset, .semitones = static_cast<double>(pair[1])});
        }
    }

    if (const juce::var& slides_json = Json::value(note_json, "slides"); !slides_json.isVoid())
    {
        if (!slides_json.isArray())
        {
            return std::unexpected{malformed("chart note slides must be an array")};
        }
        note.slides.reserve(static_cast<std::size_t>(slides_json.size()));
        for (int index = 0; index < slides_json.size(); ++index)
        {
            const juce::var& waypoint_json = slides_json[index];
            auto offset = readFraction(waypoint_json, "offset");
            if (!offset.has_value())
            {
                return std::unexpected{std::move(offset.error())};
            }
            note.slides.push_back(
                SlideWaypoint{
                    .offset = *offset,
                    .fret = Json::readOptionalInt(waypoint_json, "fret", -1),
                    .unpitched = Json::readOptionalBool(waypoint_json, "unpitched", false),
                });
        }
    }

    return note;
}

// ---- writer -------------------------------------------------------------------------------

void appendJsonString(std::string& out, const std::string& text)
{
    out += juce::JSON::toString(juce::var{juce::String{text}}, true).toStdString();
}

void appendOptionalIntArray(std::string& out, const std::vector<std::optional<int>>& values)
{
    out += '[';
    for (std::size_t index = 0; index < values.size(); ++index)
    {
        if (index > 0)
        {
            out += ", ";
        }
        out += values[index].has_value() ? std::to_string(*values[index]) : std::string{"null"};
    }
    out += ']';
}

[[nodiscard]] std::string doubleText(double value)
{
    juce::String text{value};
    if (!text.containsChar('.') && !text.containsChar('e'))
    {
        text += ".0";
    }
    return text.toStdString();
}

[[nodiscard]] std::string noteLine(const ChartNote& note)
{
    std::string line = R"({ "position": ")" + formatGridPositionToken(note.position) + '"';
    line += ", \"string\": " + std::to_string(note.string);
    line += ", \"fret\": " + std::to_string(note.fret);
    if (note.sustain.numerator != 0)
    {
        line += R"(, "sustain": ")" + formatBeatFractionToken(note.sustain) + '"';
    }
    switch (note.attack)
    {
        case NoteAttack::Pick:
        {
            break;
        }
        case NoteAttack::Hammer:
        {
            line += R"(, "attack": "hammer")";
            break;
        }
        case NoteAttack::Pull:
        {
            line += R"(, "attack": "pull")";
            break;
        }
        case NoteAttack::Tap:
        {
            line += R"(, "attack": "tap")";
            break;
        }
        case NoteAttack::Pop:
        {
            line += R"(, "attack": "pop")";
            break;
        }
        case NoteAttack::Slap:
        {
            line += R"(, "attack": "slap")";
            break;
        }
    }
    if (note.mute == NoteMute::Palm)
    {
        line += R"(, "mute": "palm")";
    }
    else if (note.mute == NoteMute::Full)
    {
        line += R"(, "mute": "full")";
    }
    if (note.harmonic == NoteHarmonic::Natural)
    {
        line += R"(, "harmonic": "natural")";
    }
    else if (note.harmonic == NoteHarmonic::Pinch)
    {
        line += R"(, "harmonic": "pinch")";
    }
    if (note.touch.has_value())
    {
        line += R"(, "touch": )" + doubleText(*note.touch);
    }
    if (note.vibrato)
    {
        line += R"(, "vibrato": true)";
    }
    if (note.tremolo)
    {
        line += R"(, "tremolo": true)";
    }
    if (note.accent)
    {
        line += R"(, "accent": true)";
    }
    if (!note.bend.empty())
    {
        line += R"(, "bend": [)";
        for (std::size_t index = 0; index < note.bend.size(); ++index)
        {
            if (index > 0)
            {
                line += ", ";
            }
            line += R"([")" + formatBeatFractionToken(note.bend[index].offset) + R"(", )" +
                    doubleText(note.bend[index].semitones) + ']';
        }
        line += ']';
    }
    if (!note.slides.empty())
    {
        line += R"(, "slides": [)";
        for (std::size_t index = 0; index < note.slides.size(); ++index)
        {
            const SlideWaypoint& waypoint = note.slides[index];
            if (index > 0)
            {
                line += ", ";
            }
            line += R"({ "offset": ")" + formatBeatFractionToken(waypoint.offset) +
                    R"(", "fret": )" + std::to_string(waypoint.fret);
            if (waypoint.unpitched)
            {
                line += R"(, "unpitched": true)";
            }
            line += " }";
        }
        line += ']';
    }
    line += " }";
    return line;
}

} // namespace

std::expected<Chart, ChartError> parseChartDocument(const std::string& text)
{
    const auto document = Json::parseUtf8Document(text);
    if (!document.has_value())
    {
        return std::unexpected{malformed("chart document is not valid JSON")};
    }

    Chart chart;
    const juce::var& root = *document;

    const juce::var& tuning_json = Json::value(root, "tuning");
    const juce::var& strings_json = Json::value(tuning_json, "strings");
    if (!strings_json.isArray())
    {
        return std::unexpected{malformed("chart tuning strings must be an array")};
    }
    chart.tuning.strings.reserve(static_cast<std::size_t>(strings_json.size()));
    for (int index = 0; index < strings_json.size(); ++index)
    {
        chart.tuning.strings.push_back(strings_json[index].toString().toStdString());
    }
    chart.tuning.capo = Json::readOptionalInt(tuning_json, "capo", 0);
    chart.tuning.cent_offset = Json::readOptionalDouble(tuning_json, "centOffset", 0.0);

    const juce::var& chords_json = Json::value(root, "chords");
    if (chords_json.isArray())
    {
        chart.templates.reserve(static_cast<std::size_t>(chords_json.size()));
        for (int index = 0; index < chords_json.size(); ++index)
        {
            const juce::var& template_json = chords_json[index];
            auto frets = readOptionalIntArray(Json::value(template_json, "frets"), "frets");
            if (!frets.has_value())
            {
                return std::unexpected{std::move(frets.error())};
            }
            auto fingers = readOptionalIntArray(Json::value(template_json, "fingers"), "fingers");
            if (!fingers.has_value())
            {
                return std::unexpected{std::move(fingers.error())};
            }
            chart.templates.push_back(
                ChordTemplate{
                    .name = Json::readOptionalString(template_json, "name", ""),
                    .frets = std::move(*frets),
                    .fingers = std::move(*fingers),
                });
        }
    }

    const juce::var& notes_json = Json::value(root, "notes");
    if (notes_json.isArray())
    {
        chart.notes.reserve(static_cast<std::size_t>(notes_json.size()));
        for (int index = 0; index < notes_json.size(); ++index)
        {
            auto note = readNote(notes_json[index]);
            if (!note.has_value())
            {
                return std::unexpected{std::move(note.error())};
            }
            chart.notes.push_back(std::move(*note));
        }
    }

    const juce::var& shapes_json = Json::value(root, "shapes");
    if (shapes_json.isArray())
    {
        chart.shapes.reserve(static_cast<std::size_t>(shapes_json.size()));
        for (int index = 0; index < shapes_json.size(); ++index)
        {
            const juce::var& shape_json = shapes_json[index];
            auto position = readPosition(shape_json);
            if (!position.has_value())
            {
                return std::unexpected{std::move(position.error())};
            }
            auto sustain = readFraction(shape_json, "sustain");
            if (!sustain.has_value())
            {
                return std::unexpected{std::move(sustain.error())};
            }
            const int chord = Json::readOptionalInt(shape_json, "chord", -1);
            if (chord < 0)
            {
                return std::unexpected{malformed("chart shape chord index is missing")};
            }
            chart.shapes.push_back(
                ChartShape{
                    .position = *position,
                    .sustain = *sustain,
                    .chord = static_cast<std::size_t>(chord),
                });
        }
    }

    const juce::var& fhps_json = Json::value(root, "fhps");
    if (fhps_json.isArray())
    {
        chart.fret_hand_positions.reserve(static_cast<std::size_t>(fhps_json.size()));
        for (int index = 0; index < fhps_json.size(); ++index)
        {
            const juce::var& fhp_json = fhps_json[index];
            auto position = readPosition(fhp_json);
            if (!position.has_value())
            {
                return std::unexpected{std::move(position.error())};
            }
            chart.fret_hand_positions.push_back(
                FretHandPosition{
                    .position = *position,
                    .fret = Json::readOptionalInt(fhp_json, "fret", 0),
                    .width = Json::readOptionalInt(fhp_json, "width", 4),
                });
        }
    }

    const juce::var& sections_json = Json::value(root, "sections");
    if (sections_json.isArray())
    {
        chart.sections.reserve(static_cast<std::size_t>(sections_json.size()));
        for (int index = 0; index < sections_json.size(); ++index)
        {
            const juce::var& section_json = sections_json[index];
            auto position = readPosition(section_json);
            if (!position.has_value())
            {
                return std::unexpected{std::move(position.error())};
            }
            chart.sections.push_back(
                ChartSection{
                    .position = *position,
                    .type = Json::readOptionalString(section_json, "type", ""),
                });
        }
    }

    return chart;
}

std::expected<Chart, ChartError> readChartDocument(const std::filesystem::path& file)
{
    const juce::File chart_file = juceFileFromPath(file);
    if (!chart_file.existsAsFile())
    {
        return std::unexpected{malformed("chart document does not exist: " + file.string())};
    }
    return parseChartDocument(chart_file.loadFileAsString().toStdString());
}

std::string chartDocumentText(const Chart& chart)
{
    std::string text = "{\n  \"formatVersion\": 1,\n";

    text += R"(  "tuning": { "strings": [)";
    for (std::size_t index = 0; index < chart.tuning.strings.size(); ++index)
    {
        if (index > 0)
        {
            text += ", ";
        }
        appendJsonString(text, chart.tuning.strings[index]);
    }
    text += "], \"capo\": " + std::to_string(chart.tuning.capo) +
            ", \"centOffset\": " + doubleText(chart.tuning.cent_offset) + " },\n";

    const auto append_array = [&text](const char* key, const auto& items, const auto& to_line) {
        text += std::string{"  \""} + key + "\": [";
        for (std::size_t index = 0; index < items.size(); ++index)
        {
            text += index == 0 ? "\n      " : ",\n      ";
            text += to_line(items[index]);
        }
        text += items.empty() ? "],\n" : "\n  ],\n";
    };

    append_array("chords", chart.templates, [](const ChordTemplate& chord_template) {
        std::string line = R"({ "name": )";
        appendJsonString(line, chord_template.name);
        line += R"(, "frets": )";
        appendOptionalIntArray(line, chord_template.frets);
        line += R"(, "fingers": )";
        appendOptionalIntArray(line, chord_template.fingers);
        line += " }";
        return line;
    });
    append_array("notes", chart.notes, noteLine);
    append_array("shapes", chart.shapes, [](const ChartShape& shape) {
        return R"({ "position": ")" + formatGridPositionToken(shape.position) +
               R"(", "sustain": ")" + formatBeatFractionToken(shape.sustain) + R"(", "chord": )" +
               std::to_string(shape.chord) + " }";
    });
    append_array("fhps", chart.fret_hand_positions, [](const FretHandPosition& fhp) {
        std::string line = R"({ "position": ")" + formatGridPositionToken(fhp.position) +
                           R"(", "fret": )" + std::to_string(fhp.fret);
        if (fhp.width != 4)
        {
            line += R"(, "width": )" + std::to_string(fhp.width);
        }
        line += " }";
        return line;
    });
    append_array("sections", chart.sections, [](const ChartSection& section) {
        std::string line =
            R"({ "position": ")" + formatGridPositionToken(section.position) + R"(", "type": )";
        appendJsonString(line, section.type);
        line += " }";
        return line;
    });

    // Drop the trailing comma from the final array before closing the document.
    if (text.ends_with(",\n"))
    {
        text.erase(text.size() - 2);
        text += "\n";
    }
    text += "}\n";
    return text;
}

std::expected<void, ChartError> writeChartDocument(
    const std::filesystem::path& file, const Chart& chart)
{
    const juce::File chart_file = juceFileFromPath(file);
    if (!chart_file.getParentDirectory().createDirectory())
    {
        return std::unexpected{malformed(
            "could not create the chart document directory: " + file.string())};
    }
    if (!chart_file.replaceWithText(juce::String::fromUTF8(chartDocumentText(chart).c_str())))
    {
        return std::unexpected{malformed("could not write the chart document: " + file.string())};
    }
    return {};
}

} // namespace rock_hero::common::core
