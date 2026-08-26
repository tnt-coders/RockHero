#include "chart/chart_document.h"

#include <algorithm>
#include <array>
#include <compare>
#include <optional>
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

// One interval statement: a required offset plus any SUBSET of the channels. Each channel is
// presence-keyed, because absence is a meaning — the waypoint says nothing about that channel and
// the reading passes through it — rather than a defaulted value. Every channel is therefore
// type-checked in place, like the note scalars: a wrong-typed fret read as absent would silently
// turn an authored glide into a pass-through, and the note would then validate clean.
[[nodiscard]] std::expected<Waypoint, ChartError> readWaypoint(const juce::var& waypoint_json)
{
    auto offset = readFraction(waypoint_json, "offset");
    if (!offset.has_value())
    {
        return std::unexpected{std::move(offset.error())};
    }
    struct ChannelRule
    {
        std::string_view key;
        bool (*matches)(const juce::var&);
    };
    constexpr std::array channel_rules{
        ChannelRule{.key = "fret", .matches = [](const juce::var& v) { return v.isInt(); }},
        ChannelRule{
            .key = "bend", .matches = [](const juce::var& v) { return v.isDouble() || v.isInt(); }
        },
        ChannelRule{.key = "vibrato", .matches = [](const juce::var& v) { return v.isBool(); }},
    };
    for (const auto& [key, matches] : channel_rules)
    {
        const juce::var& property = Json::value(waypoint_json, key);
        if (!property.isVoid() && !matches(property))
        {
            return std::unexpected{malformed(
                "chart waypoint \"" + std::string{key} + "\" has the wrong type")};
        }
    }
    // A waypoint stating no channel at all is refused by validateChartNoteAlone rather than here:
    // this reader answers what the document SAYS, and an empty statement is a legality question
    // the one rules authority owns.
    return Waypoint{
        .offset = *offset,
        .fret = Json::value(waypoint_json, "fret").isVoid()
                    ? std::nullopt
                    : std::optional{Json::readOptionalInt(waypoint_json, "fret", 0)},
        .bend = Json::value(waypoint_json, "bend").isVoid()
                    ? std::nullopt
                    : std::optional{Json::readOptionalDouble(waypoint_json, "bend", 0.0)},
        .vibrato = Json::value(waypoint_json, "vibrato").isVoid()
                       ? std::nullopt
                       : std::optional{Json::readOptionalBool(waypoint_json, "vibrato", false)},
    };
}

[[nodiscard]] std::expected<ChartNote, ChartError> readNote(const juce::var& note_json)
{
    auto position = readPosition(note_json);
    if (!position.has_value())
    {
        return std::unexpected{std::move(position.error())};
    }

    // Spellings the format no longer has, refused BEFORE any type check so a document that
    // predates a change reports the re-import remedy instead of a bare "wrong type" — three of
    // these keys still exist under a different SHAPE, which a type message would describe without
    // naming the fix. Every project is fresh and nothing legacy is preserved (chart_document.h),
    // so each row exists to fail loudly, not to support the old form; delete a row once the
    // packages carrying it are re-imported.
    struct RemovedSpelling
    {
        std::string_view key;
        // The shape that identifies the OLD form. A key that is simply gone matches anything
        // present; one that survived in a new shape matches only its old one.
        bool (*was)(const juce::var&);
        std::string_view remedy;
    };
    constexpr std::array removed_spellings{
        RemovedSpelling{
            .key = "mute",
            .was = [](const juce::var&) { return true; },
            // The one mute axis became two independent flags: a hand can palm the strings and
            // deaden a string at the same time, which one enum could not say.
            .remedy = "re-import the package to get \"palmMute\" and \"dead\"",
        },
        RemovedSpelling{
            .key = "harmonic",
            .was = [](const juce::var&) { return true; },
            // The harmonic field is gone: a node asserts the harmonic and `attack` says which
            // hand damps it.
            .remedy = "re-import the package to get \"harmonicNode\" (and \"attack\": \"pinch\")",
        },
        RemovedSpelling{
            .key = "touch",
            .was = [](const juce::var&) { return true; },
            .remedy = "re-import the package to get \"harmonicNode\" (and \"attack\": \"pinch\")",
        },
        RemovedSpelling{
            .key = "accent",
            .was = [](const juce::var&) { return true; },
            // The accent bool became one end of the emphasis axis, whose other end is the ghost.
            .remedy = "re-import the package to get \"emphasis\": \"accent\"",
        },
        RemovedSpelling{
            .key = "slides",
            .was = [](const juce::var&) { return true; },
            // Slide waypoints became the one interval-payload array, which every channel shares.
            .remedy = "re-import the package to get \"waypoints\"",
        },
        RemovedSpelling{
            .key = "bend",
            .was = [](const juce::var& v) { return v.isArray(); },
            // The bend CURVE dissolved: its onset value is this key as a number, and every later
            // value is a waypoint's bend channel.
            .remedy = "re-import the package to get the onset \"bend\" value and \"waypoints\"",
        },
        RemovedSpelling{
            .key = "slideOut",
            .was = [](const juce::var& v) { return v.isObject(); },
            // A slide-out ends the ring by definition, so the object's stored offset is gone and
            // the key is now the gestured fret itself.
            .remedy = "re-import the package to get \"slideOut\" as the gestured fret",
        },
    };
    for (const auto& [removed_key, was, remedy] : removed_spellings)
    {
        const juce::var& property = Json::value(note_json, removed_key);
        if (!property.isVoid() && was(property))
        {
            return std::unexpected{malformed(
                "chart note uses the removed \"" + std::string{removed_key} + "\" form; " +
                std::string{remedy})};
        }
    }

    // A chart means exactly what it says, so a property that is PRESENT but of the wrong JSON type
    // is malformed rather than absent. The lenient readers below are built for draft metadata,
    // where a fallback beats a refusal; on a note a fallback silently changes the music — a numeric
    // "attack" read as a plain pick, `"sustain": 2` read as no tail at all, `"vibrato": 1` read as
    // no vibrato — and the note then validates clean, so nothing downstream can notice. Every
    // scalar note property has a row, so a reader added below needs its row here; the nested
    // waypoint objects carry the same rule in \ref readWaypoint, per channel, because absence is a
    // meaning there and a wrong-typed fret read as absent would silently turn a glide into a
    // pass-through.
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
        ScalarRule{
            .key = "bend", .matches = [](const juce::var& v) { return v.isDouble() || v.isInt(); }
        },
        ScalarRule{.key = "tremolo", .matches = [](const juce::var& v) { return v.isBool(); }},
        ScalarRule{.key = "emphasis", .matches = [](const juce::var& v) { return v.isString(); }},
        ScalarRule{.key = "slideOut", .matches = [](const juce::var& v) { return v.isInt(); }},
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

    note.palm_mute = Json::readOptionalBool(note_json, "palmMute", false);
    note.dead = Json::readOptionalBool(note_json, "dead", false);
    note.harmonic_node = Json::tryReadDouble(note_json, "harmonicNode");

    note.vibrato = Json::readOptionalBool(note_json, "vibrato", false);
    // The onset value of the bend channel; zero is the default and the unbent onset, so an absent
    // key and a written 0 mean exactly the same thing and neither is a second spelling of the
    // other.
    note.bend = Json::readOptionalDouble(note_json, "bend", 0.0);
    note.tremolo = Json::readOptionalBool(note_json, "tremolo", false);

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

    if (const juce::var& waypoints_json = Json::value(note_json, "waypoints");
        !waypoints_json.isVoid())
    {
        if (!waypoints_json.isArray())
        {
            return std::unexpected{malformed("chart note waypoints must be an array")};
        }
        note.waypoints.reserve(static_cast<std::size_t>(waypoints_json.size()));
        for (int index = 0; index < waypoints_json.size(); ++index)
        {
            const juce::var& waypoint_json = waypoints_json[index];
            auto waypoint = readWaypoint(waypoint_json);
            if (!waypoint.has_value())
            {
                return std::unexpected{std::move(waypoint.error())};
            }
            note.waypoints.push_back(*waypoint);
        }
    }

    // The gestured fret alone: a slide-out releases off the note's END, so its moment is the ring's
    // and it stores none of its own (chart.h). Pitched glides — shift and legato alike — are the
    // fret channel of ordinary waypoints above.
    if (!Json::value(note_json, "slideOut").isVoid())
    {
        note.slide_out = Json::readOptionalInt(note_json, "slideOut", -1);
    }

    return note;
}

[[nodiscard]] std::expected<ChartHoldMarker, ChartError> readHoldMarker(
    const juce::var& marker_json)
{
    auto position = readPosition(marker_json);
    if (!position.has_value())
    {
        return std::unexpected{std::move(position.error())};
    }
    // Typed exactly like the note scalars, and for the same reason: a wrong-typed `fret` read as
    // absent would silently turn an authored stop into "ask the notes for it", which then resolves
    // to nothing and draws nothing — a marker that vanished with no word said.
    for (const char* const key : {"string", "fret"})
    {
        const juce::var& property = Json::value(marker_json, key);
        if (!property.isVoid() && !property.isInt())
        {
            return std::unexpected{malformed(
                "chart hold marker \"" + std::string{key} + "\" has the wrong type")};
        }
    }
    return ChartHoldMarker{
        .position = *position,
        .string = Json::readOptionalInt(marker_json, "string", 0),
        // Absence is the α form and carries meaning, so there is no default to read: the stop comes
        // from the note that supplies it.
        .fret = Json::value(marker_json, "fret").isVoid()
                    ? std::nullopt
                    : std::optional{Json::readOptionalInt(marker_json, "fret", 0)},
    };
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
    // Zero is the unbent onset and the field's default, so it never writes — the same elision every
    // defaulted note property takes.
    if (std::is_neq(note.bend <=> 0.0))
    {
        line += R"(, "bend": )" + doubleText(note.bend);
    }
    if (!note.waypoints.empty())
    {
        line += R"(, "waypoints": [)";
        for (std::size_t index = 0; index < note.waypoints.size(); ++index)
        {
            const Waypoint& waypoint = note.waypoints[index];
            if (index > 0)
            {
                line += ", ";
            }
            line += R"({ "offset": ")" + formatBeatFractionToken(waypoint.offset) + '"';
            // Every STATED channel is written, values that look like defaults included: a bend of
            // zero is a release back to rest and a false vibrato is a shake ENDING, so eliding
            // either would delete the statement rather than shorten it. Absence is what says
            // nothing was stated.
            //
            // Each channel is bound to a local so its check and its access are provably the same
            // object, which the CI-only optional-access checker does not credit across two
            // separate reads of an indexed element.
            const std::optional<int>& fret = waypoint.fret;
            if (fret.has_value())
            {
                line += R"(, "fret": )" + std::to_string(*fret);
            }
            const std::optional<double>& bend = waypoint.bend;
            if (bend.has_value())
            {
                line += R"(, "bend": )" + doubleText(*bend);
            }
            const std::optional<bool>& vibrato = waypoint.vibrato;
            if (vibrato.has_value())
            {
                line += R"(, "vibrato": )" + std::string{*vibrato ? "true" : "false"};
            }
            line += " }";
        }
        line += ']';
    }
    if (const int* const slide_out = slideOutFretOrNull(note); slide_out != nullptr)
    {
        line += R"(, "slideOut": )" + std::to_string(*slide_out);
    }
    line += " }";
    return line;
}

[[nodiscard]] std::string holdMarkerLine(const ChartHoldMarker& marker)
{
    std::string line = R"({ "position": ")" + formatGridPositionToken(marker.position) + '"';
    line += ", \"string\": " + std::to_string(marker.string);
    // Omitted where a note in the span supplies it. Absence is a MEANING here rather than a
    // defaulted value — writing the resolved fret out would author exactly the second copy this
    // record exists not to hold.
    if (marker.fret.has_value())
    {
        line += ", \"fret\": " + std::to_string(*marker.fret);
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

    const juce::var& markers_json = Json::value(root, "holdMarkers");
    if (markers_json.isArray())
    {
        chart.hold_markers.reserve(static_cast<std::size_t>(markers_json.size()));
        for (int index = 0; index < markers_json.size(); ++index)
        {
            auto marker = readHoldMarker(markers_json[index]);
            if (!marker.has_value())
            {
                return std::unexpected{std::move(marker.error())};
            }
            chart.hold_markers.push_back(*marker);
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
    append_array("holdMarkers", chart.hold_markers, holdMarkerLine);
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
