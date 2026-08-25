#include "chart/chart_document.h"

#include <algorithm>
#include <array>
#include <rock_hero/common/core/chart/chart_legato.h>
#include <rock_hero/common/core/chart/chart_tokens.h>
#include <rock_hero/common/core/shared/json.h>
#include <rock_hero/common/core/shared/juce_path.h>
#include <vector>

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

[[nodiscard]] std::expected<ChartNote, ChartError> readNote(const juce::var& note_json)
{
    auto position = readPosition(note_json);
    if (!position.has_value())
    {
        return std::unexpected{std::move(position.error())};
    }

    // A chart means exactly what it says, so a property that is PRESENT but of the wrong JSON type
    // is malformed rather than absent. The lenient readers below are built for draft metadata,
    // where a fallback beats a refusal; on a note a fallback silently changes the music — a numeric
    // "attack" read as a plain pick, `"sustain": 2` read as no tail at all, `"vibrato": 1` read as
    // no vibrato — and the note then validates clean, so nothing downstream can notice. Every
    // scalar note property has a row, so a reader added below needs its row here; the nested bend,
    // slide, and slide-out objects check their own shapes in place, and a wrong-typed fret inside
    // them reads as -1, which validation refuses loudly rather than silently.
    struct ScalarRule
    {
        std::string_view key;
        bool (*matches)(const juce::var&);
    };
    constexpr std::array scalar_rules{
        ScalarRule{.key = "string", .matches = [](const juce::var& v) { return v.isInt(); }},
        ScalarRule{.key = "fret", .matches = [](const juce::var& v) { return v.isInt(); }},
        ScalarRule{.key = "sustain", .matches = [](const juce::var& v) { return v.isString(); }},
        ScalarRule{.key = "attack", .matches = [](const juce::var& v) { return v.isString(); }},
        ScalarRule{.key = "palmMute", .matches = [](const juce::var& v) { return v.isBool(); }},
        ScalarRule{.key = "dead", .matches = [](const juce::var& v) { return v.isBool(); }},
        ScalarRule{
            .key = "harmonicNode",
            .matches = [](const juce::var& v) { return v.isDouble() || v.isInt(); }
        },
        ScalarRule{.key = "vibrato", .matches = [](const juce::var& v) { return v.isBool(); }},
        ScalarRule{.key = "tremolo", .matches = [](const juce::var& v) { return v.isBool(); }},
        ScalarRule{.key = "emphasis", .matches = [](const juce::var& v) { return v.isString(); }},
    };
    for (const auto& [key, matches] : scalar_rules)
    {
        const juce::var& property = Json::value(note_json, key);
        if (!property.isVoid() && !matches(property))
        {
            return std::unexpected{malformed(
                "chart note \"" + std::string{key} + "\" has the wrong type")};
        }
    }

    ChartNote note;
    note.position = *position;
    note.string = Json::readOptionalInt(note_json, "string", 0);
    note.fret = Json::readOptionalInt(note_json, "fret", -1);
    // Required, with no default: every note rings for some length, so a missing key is a missing
    // fact rather than "no tail" — reading it as zero would silently invent the one datum the
    // model cannot derive. This is also the path a chart written before the duration model
    // actually takes, and the only one: that writer OMITTED the key on every tail-less note, which
    // is most of them, so the message carries the re-import remedy exactly like the removed-key
    // tripwires below. (The positive-sustain rule states the same remedy for a zero that is
    // written out, which no version of the writer ever emitted.)
    if (Json::value(note_json, "sustain").isVoid())
    {
        return std::unexpected{malformed(
            "chart note is missing \"sustain\"; re-import the package to get the note's actual "
            "ring duration")};
    }
    auto sustain = readFraction(note_json, "sustain");
    if (!sustain.has_value())
    {
        return std::unexpected{std::move(sustain.error())};
    }
    note.sustain = *sustain;

    const std::string attack = Json::readOptionalString(note_json, "attack", "");
    if (attack == "legato")
    {
        note.attack = NoteAttack::Legato;
    }
    else if (attack == "pinch")
    {
        note.attack = NoteAttack::Pinch;
    }
    else if (attack == "leftTap")
    {
        note.attack = NoteAttack::LeftTap;
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
    else if (attack == "pickSlide")
    {
        note.attack = NoteAttack::PickSlide;
    }
    else if (!attack.empty())
    {
        return std::unexpected{malformed("chart note attack is unknown: " + attack)};
    }

    // The one mute axis became two independent flags: a hand can palm the strings and deaden a
    // string at the same time, which one enum could not say. A document still carrying the old key
    // predates that and would otherwise load with every mute silently dropped, so refuse it and
    // name the fix — the same tripwire the harmonic/touch removal got, and deleted on the same
    // schedule.
    if (!Json::value(note_json, "mute").isVoid())
    {
        return std::unexpected{malformed(
            "chart note uses the removed \"mute\" field; re-import the package to get "
            "\"palmMute\" and \"dead\"")};
    }

    note.palm_mute = Json::readOptionalBool(note_json, "palmMute", false);
    note.dead = Json::readOptionalBool(note_json, "dead", false);

    // The harmonic field is gone: a node asserts the harmonic and `attack` says which hand damps
    // it. A document still carrying either old key predates that and would otherwise load with its
    // harmonics silently dropped, so refuse it and name the fix. Delete this once the packages are
    // re-imported — it exists to fail loudly, not to support the old shape.
    if (!Json::readOptionalString(note_json, "harmonic", "").empty() ||
        Json::tryReadDouble(note_json, "touch").has_value())
    {
        return std::unexpected{malformed(
            "chart note uses the removed harmonic/touch fields; re-import the package to get "
            "\"harmonicNode\" (and \"attack\": \"pinch\")")};
    }

    note.harmonic_node = Json::tryReadDouble(note_json, "harmonicNode");

    note.vibrato = Json::readOptionalBool(note_json, "vibrato", false);
    note.tremolo = Json::readOptionalBool(note_json, "tremolo", false);

    // The accent bool became one end of the emphasis axis, whose other end is the ghost note.
    // A document still carrying the old key predates that and would otherwise load with every
    // accent silently stripped, so refuse it and name the fix — the same tripwire the
    // harmonic/touch removal got, and deleted on the same schedule.
    if (!Json::value(note_json, "accent").isVoid())
    {
        return std::unexpected{malformed(
            "chart note uses the removed \"accent\" field; re-import the package to get "
            "\"emphasis\": \"accent\"")};
    }

    // Present means it must name a token: `normal` is refused along with anything unknown,
    // because absence already says it — the rule the absent pick attack follows. Keyed on
    // PRESENCE rather than on emptiness so `""` is refused too, instead of slipping through as
    // the value the reader cannot tell from an absent key.
    if (!Json::value(note_json, "emphasis").isVoid())
    {
        const std::string emphasis = Json::readOptionalString(note_json, "emphasis", "");
        if (emphasis == "accent")
        {
            note.emphasis = NoteEmphasis::Accent;
        }
        else if (emphasis == "ghost")
        {
            note.emphasis = NoteEmphasis::Ghost;
        }
        else
        {
            return std::unexpected{malformed("chart note emphasis is unknown: " + emphasis)};
        }
    }

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
            // The semitone side is checked as strictly as the offset side: an unchecked cast made
            // `["0", "half"]` a flat zero-semitone bend that validates clean.
            if (!pair.isArray() || pair.size() != 2 || !pair[0].isString() ||
                !(pair[1].isDouble() || pair[1].isInt()))
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
                });
        }
    }

    // The unpitched slide-out owns its end offset and gestured fret (no landing note exists);
    // pitched glides — shift and legato alike — are ordinary slide waypoints above.
    if (const juce::var& out_json = Json::value(note_json, "slideOut"); !out_json.isVoid())
    {
        auto offset = readFraction(out_json, "offset");
        if (!offset.has_value())
        {
            return std::unexpected{std::move(offset.error())};
        }
        note.slide_out = SlideOut{
            .offset = *offset,
            .fret = Json::readOptionalInt(out_json, "fret", -1),
        };
    }

    return note;
}

// ---- writer -------------------------------------------------------------------------------

void appendJsonString(std::string& out, const std::string& text)
{
    out += juce::JSON::toString(juce::var{juce::String{text}}, true).toStdString();
}

// A chart's doubles are measurements — a harmonic node is `12 * log2(partial)`, a bend height a
// fraction of a step — so the writer needs the shared round-trip-exact form. `juce::String{double}`
// was NOT that: it leaves the stream at its default six significant digits, which silently rounded
// every node and semitone, and could round an out-of-range value back into range on the way out.
[[nodiscard]] std::string doubleText(double value)
{
    return Json::numberText(value);
}

[[nodiscard]] std::string noteLine(const ChartNote& note)
{
    std::string line = R"({ "position": ")" + formatGridPositionToken(note.position) + '"';
    line += ", \"string\": " + std::to_string(note.string);
    line += ", \"fret\": " + std::to_string(note.fret);
    // Always emitted: the ring is a note's own fact, and the reader refuses a document that omits
    // it, so there is no shorter form to elide into.
    line += R"(, "sustain": ")" + formatBeatFractionToken(note.sustain) + '"';
    switch (note.attack)
    {
        case NoteAttack::Pick:
        {
            break;
        }
        case NoteAttack::Pinch:
        {
            line += R"(, "attack": "pinch")";
            break;
        }
        case NoteAttack::Legato:
        {
            line += R"(, "attack": "legato")";
            break;
        }
        case NoteAttack::LeftTap:
        {
            line += R"(, "attack": "leftTap")";
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
        case NoteAttack::PickSlide:
        {
            line += R"(, "attack": "pickSlide")";
            break;
        }
    }
    if (note.palm_mute)
    {
        line += R"(, "palmMute": true)";
    }
    if (note.dead)
    {
        line += R"(, "dead": true)";
    }
    if (note.harmonic_node.has_value())
    {
        line += R"(, "harmonicNode": )" + doubleText(*note.harmonic_node);
    }
    if (note.vibrato)
    {
        line += R"(, "vibrato": true)";
    }
    if (note.tremolo)
    {
        line += R"(, "tremolo": true)";
    }
    // Normal is the implied default and never written, so the common note costs nothing. A
    // switch without a default, like the attack writer above: a value added to the enum has to
    // be given a token here or the build stops, rather than serializing as silently nothing.
    switch (note.emphasis)
    {
        case NoteEmphasis::Normal:
        {
            break;
        }
        case NoteEmphasis::Ghost:
        {
            line += R"(, "emphasis": "ghost")";
            break;
        }
        case NoteEmphasis::Accent:
        {
            line += R"(, "emphasis": "accent")";
            break;
        }
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
                    R"(", "fret": )" + std::to_string(waypoint.fret) + " }";
        }
        line += ']';
    }
    if (note.slide_out.has_value())
    {
        line += R"(, "slideOut": { "offset": ")" + formatBeatFractionToken(note.slide_out->offset) +
                R"(", "fret": )" + std::to_string(note.slide_out->fret) + " }";
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

    // The single chart-document version gate, mirroring the other formats: formatVersion 1 is the
    // only supported revision, and no other call site may test the chart version. The format
    // itself changes in place under this number — every project is fresh, nothing legacy is
    // preserved, and stale documents are simply re-imported.
    if (Json::readOptionalInt(root, "formatVersion", 0) != 1)
    {
        return std::unexpected{malformed("chart document formatVersion must be 1")};
    }

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
    // Typed like the note scalars: a wrong-typed capo silently read as 0 would then have every
    // capo-relative rule — the floors, the hand windows, the node stops — measured against the
    // wrong floor, and the chart would validate clean.
    const juce::var& capo_json = Json::value(tuning_json, "capo");
    if (!capo_json.isVoid() && !capo_json.isInt())
    {
        return std::unexpected{malformed("chart tuning \"capo\" has the wrong type")};
    }
    const juce::var& cent_offset_json = Json::value(tuning_json, "centOffset");
    if (!cent_offset_json.isVoid() && !cent_offset_json.isDouble() && !cent_offset_json.isInt())
    {
        return std::unexpected{malformed("chart tuning \"centOffset\" has the wrong type")};
    }
    chart.tuning.capo = Json::readOptionalInt(tuning_json, "capo", 0);
    chart.tuning.cent_offset = Json::readOptionalDouble(tuning_json, "centOffset", 0.0);

    // The posture table and the spans that indexed it are gone: both are derived from the notes
    // now (deriveChartShapes), so a document carrying them states a second, unverifiable copy of
    // something the notes already say. Refused rather than ignored, the same tripwire the removed
    // note fields get, so an un-reimported package fails loudly with the fix named instead of
    // loading with a stale picture silently discarded. Delete this once the packages are
    // re-imported — it exists to fail loudly, not to support the old shape.
    if (!Json::value(root, "chords").isVoid() || !Json::value(root, "shapes").isVoid())
    {
        return std::unexpected{malformed(
            "chart uses the removed \"chords\"/\"shapes\" fields; re-import the package to derive "
            "the hand-posture spans from the notes")};
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
    // The order is a document rule, checked here rather than left to the validator, because
    // everything between the two reads the stream as a sorted one: the normalizer's same-string
    // bound binary-searches it (`sustainBoundOf`), presentation partitions onset groups by
    // adjacency, and the resolver's forward walk calls the previous note on a string its
    // predecessor. Refused rather than sorted, the same posture the persisted sections take — and
    // refusing it BEFORE normalization is what keeps an out-of-order document from being
    // diagnosed as some garbage truncation the search invented. Duplicate onsets stay the
    // validator's: a sortedness test cannot see them.
    if (!std::ranges::is_sorted(chart.notes, chartNoteOrderLess))
    {
        return std::unexpected{malformed("chart notes must be sorted by position and string")};
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
            for (const char* const key : {"fret", "width"})
            {
                const juce::var& property = Json::value(fhp_json, key);
                if (!property.isVoid() && !property.isInt())
                {
                    return std::unexpected{malformed(
                        "chart hand position \"" + std::string{key} + "\" has the wrong type")};
                }
            }
            // The width's default is the type's own, stated once in chart.h.
            chart.fret_hand_positions.push_back(
                FretHandPosition{
                    .position = *position,
                    .fret = Json::readOptionalInt(fhp_json, "fret", 0),
                    .width = Json::readOptionalInt(fhp_json, "width", FretHandPosition{}.width),
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

namespace
{

// Renders a chart already in its document form (documentChart): every note in saved form, every
// claim settled. Nothing here converts, so the text is exactly the chart it is handed.
[[nodiscard]] std::string renderChartDocument(const Chart& chart)
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

    append_array("notes", chart.notes, noteLine);
    append_array("fhps", chart.fret_hand_positions, [](const FretHandPosition& fhp) {
        std::string line = R"({ "position": ")" + formatGridPositionToken(fhp.position) +
                           R"(", "fret": )" + std::to_string(fhp.fret);
        if (fhp.width != FretHandPosition{}.width)
        {
            line += R"(, "width": )" + std::to_string(fhp.width);
        }
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

} // namespace

Chart documentChart(const Chart& chart, const TempoMap& tempo_map)
{
    // The document stream is the RESOLVED, SAVED one: an unjustifiable claim leaves as the pick it
    // plays as, and a latent override never reaches the file — the memory-richer-than-file design,
    // applied at the one seam between the two. In memory the claim survives, so re-justifying it
    // later is a neighbour edit rather than a re-authoring.
    Chart document = chart;
    static_cast<void>(sweepUnjustifiedLegato(document.notes, tempo_map));
    for (ChartNote& note : document.notes)
    {
        note = savedChartNote(note);
    }
    return document;
}

std::string chartDocumentText(const Chart& chart, const TempoMap& tempo_map)
{
    return renderChartDocument(documentChart(chart, tempo_map));
}

std::expected<void, ChartError> writeChartDocument(
    const std::filesystem::path& file, const Chart& chart, const TempoMap& tempo_map)
{
    // The writer refuses to emit a document the reader would refuse: memory is valid by
    // construction (load normalizes, the edit verbs refuse), so this never fires unless a verb let
    // something through — in which case it has caught a defect, and persisting the file would
    // only move the failure to the next open.
    const Chart document = documentChart(chart, tempo_map);
    if (auto valid = validateChartRules(document, tempo_map); !valid.has_value())
    {
        return std::unexpected{ChartError{
            .code = valid.error().code,
            .message = "the chart would not load back (an edit let an invalid note through; "
                       "please report this): " +
                       valid.error().message,
        }};
    }
    const juce::File chart_file = juceFileFromPath(file);
    if (!chart_file.getParentDirectory().createDirectory())
    {
        return std::unexpected{malformed(
            "could not create the chart document directory: " + file.string())};
    }
    if (!chart_file.replaceWithText(juce::String::fromUTF8(renderChartDocument(document).c_str())))
    {
        return std::unexpected{malformed("could not write the chart document: " + file.string())};
    }
    return {};
}

} // namespace rock_hero::common::core
