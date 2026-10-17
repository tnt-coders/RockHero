#include "chart/chart_document.h"

#include <algorithm>
#include <array>
#include <compare>
#include <optional>
#include <rock_hero/common/core/chart/chart_legato.h>
#include <rock_hero/common/core/chart/chart_tokens.h>
#include <rock_hero/common/core/shared/json.h>
#include <rock_hero/common/core/shared/juce_path.h>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace rock_hero::common::core
{

namespace
{

// The chart document's one format version, read by both the reader's gate and the writer.
constexpr int g_chart_format_version{1};

[[nodiscard]] ChartError malformed(const std::string& detail)
{
    return ChartError{.code = ChartErrorCode::MalformedDocument, .message = detail};
}

// Reads the place under `key`. A missing or wrong-typed key reads as the empty token, which no
// place parses, so it is refused with the malformed one.
[[nodiscard]] std::expected<GridPosition, ChartError> readPlace(
    const juce::var& object, const char* key, const TempoMap& tempo_map)
{
    const std::string text = Json::readOptionalString(object, key, "");
    const auto position = parsePlaceToken(tempo_map, text);
    if (!position.has_value())
    {
        return std::unexpected{malformed(
            std::string{"chart place is not a whole-note place: "} + key + "=" + text)};
    }
    return *position;
}

[[nodiscard]] std::expected<Fraction, ChartError> readFraction(
    const juce::var& object, const char* key)
{
    const std::string text = Json::readOptionalString(object, key, "");
    const auto value = parseFractionToken(text);
    if (!value.has_value())
    {
        return std::unexpected{malformed(
            std::string{"chart fraction token is malformed: "} + key + "=" + text)};
    }
    return *value;
}

// The vibrato channel's tokens, in both directions. One table so the reader and the writer cannot
// disagree about a spelling. `None` has no word: a leg without vibrato omits the key, onset and
// keyframe alike, so the table names the widths alone.
constexpr std::array<std::pair<std::string_view, VibratoState>, 2> g_vibrato_tokens{{
    {"narrow", VibratoState::Narrow},
    {"wide", VibratoState::Wide},
}};

[[nodiscard]] std::optional<VibratoState> parseVibratoToken(const std::string_view token)
{
    for (const auto& [spelling, state] : g_vibrato_tokens)
    {
        if (token == spelling)
        {
            return state;
        }
    }
    return std::nullopt;
}

[[nodiscard]] std::string_view vibratoToken(const VibratoState state)
{
    for (const auto& [spelling, candidate] : g_vibrato_tokens)
    {
        if (candidate == state)
        {
            return spelling;
        }
    }
    // Total over the widths; `None` is never written, so reaching here is a caller bug, and
    // spelling it as either width would write vibrato nobody authored.
    std::unreachable();
}

// Reads an object's `vibrato` key, which the onset and a keyframe spell identically: an absent key
// is `None`, and a present one must name a width — anything else, `off` included, is unknown.
[[nodiscard]] std::expected<VibratoState, ChartError> readVibrato(
    const juce::var& object, const std::string_view owner)
{
    if (Json::value(object, "vibrato").isVoid())
    {
        return VibratoState::None;
    }
    const std::string token = Json::readOptionalString(object, "vibrato", "");
    const std::optional<VibratoState> width = parseVibratoToken(token);
    if (!width.has_value())
    {
        return std::unexpected{malformed(
            "chart " + std::string{owner} + " vibrato is unknown: " + token)};
    }
    return *width;
}

// One interval statement: a required offset plus any SUBSET of the channels. Each channel is
// presence-keyed, because absence is a meaning — the keyframe says nothing about that channel and
// the reading passes through it — rather than a defaulted value. Every channel is therefore
// type-checked in place, like the note scalars: a wrong-typed fret read as absent would silently
// turn an authored glide into a pass-through, and the note would then validate clean.
[[nodiscard]] std::expected<Keyframe, ChartError> readKeyframe(const juce::var& keyframe_json)
{
    auto offset = readFraction(keyframe_json, "offset");
    if (!offset.has_value())
    {
        return std::unexpected{std::move(offset.error())};
    }
    struct ChannelRule
    {
        std::string_view key;
        bool (*matches)(const juce::var&);
        // The channel's REMOVED shape and the fix for it — the keyframe twin of the note's removed
        // spellings. Null where a channel has no removed shape, so a wrong-typed value reports the
        // plain type message.
        bool (*was)(const juce::var&);
        std::string_view remedy;
    };
    constexpr std::array channel_rules{
        ChannelRule{
            .key = "fret",
            .matches = [](const juce::var& v) { return v.isInt(); },
            .was = nullptr,
            .remedy = {},
        },
        ChannelRule{
            .key = "bend",
            .matches = [](const juce::var& v) { return v.isDouble() || v.isInt(); },
            .was = nullptr,
            .remedy = {},
        },
        ChannelRule{
            .key = "vibrato",
            .matches = [](const juce::var& v) { return v.isString(); },
            // The vibrato is an AXIS with a width: the removed bool could say only that the string
            // vibrated, and never how wide.
            .was = [](const juce::var& v) { return v.isBool(); },
            .remedy = R"(re-import the package to get "vibrato": "narrow")",
        },
    };
    for (const auto& [key, matches, was, remedy] : channel_rules)
    {
        const juce::var& property = Json::value(keyframe_json, key);
        if (property.isVoid())
        {
            continue;
        }
        if (was != nullptr && was(property))
        {
            return std::unexpected{malformed(
                "chart keyframe uses the removed \"" + std::string{key} + "\" form; " +
                std::string{remedy})};
        }
        if (!matches(property))
        {
            return std::unexpected{malformed(
                "chart keyframe \"" + std::string{key} + "\" has the wrong type")};
        }
    }
    auto vibrato = readVibrato(keyframe_json, "keyframe");
    if (!vibrato.has_value())
    {
        return std::unexpected{std::move(vibrato.error())};
    }
    // A keyframe stating no channel at all is no reading error: a bare keyframe begins an
    // unvibrated leg, and whether it says anything is the commit law's question, which the load
    // normalizer answers (stripSilentKeyframes).
    return Keyframe{
        .offset = *offset,
        .fret = Json::value(keyframe_json, "fret").isVoid()
                    ? std::nullopt
                    : std::optional{Json::readOptionalInt(keyframe_json, "fret", 0)},
        .bend = Json::value(keyframe_json, "bend").isVoid()
                    ? std::nullopt
                    : std::optional{Json::readOptionalDouble(keyframe_json, "bend", 0.0)},
        .vibrato = *vibrato,
    };
}

[[nodiscard]] std::expected<ChartNote, ChartError> readNote(
    const juce::var& note_json, const TempoMap& tempo_map)
{
    auto position = readPlace(note_json, "at", tempo_map);
    if (!position.has_value())
    {
        return std::unexpected{std::move(position.error())};
    }

    // Removed spellings, refused BEFORE any type check so an out-of-date document reports the
    // re-import remedy instead of a bare "wrong type" — some of these keys exist under a different
    // SHAPE, which a type message would describe without naming the fix. Nothing legacy is read,
    // so each row exists to fail loudly, not to support the old form; delete a row once no
    // package carries it.
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
            // Muting is two independent flags: a hand can palm the strings and deaden a string at
            // the same time, which one enum could not say.
            .remedy = R"(re-import the package to get "palmMute" and "dead")",
        },
        RemovedSpelling{
            .key = "harmonic",
            .was = [](const juce::var&) { return true; },
            // No harmonic field exists: a node asserts the harmonic and `attack` says which hand
            // damps it.
            .remedy = R"(re-import the package to get "harmonicNode" (and "attack": "pinch"))",
        },
        RemovedSpelling{
            .key = "touch",
            .was = [](const juce::var&) { return true; },
            .remedy = R"(re-import the package to get "harmonicNode" (and "attack": "pinch"))",
        },
        RemovedSpelling{
            .key = "accent",
            .was = [](const juce::var&) { return true; },
            // Accent is one end of the emphasis axis, whose other end is the ghost.
            .remedy = R"(re-import the package to get "emphasis": "accent")",
        },
        RemovedSpelling{
            .key = "slides",
            .was = [](const juce::var&) { return true; },
            // Slide stops live in the one interval-payload array, which every channel shares.
            .remedy = "re-import the package to get \"keyframes\"",
        },
        RemovedSpelling{
            .key = "waypoints",
            .was = [](const juce::var&) { return true; },
            // The keyframes array under its old NAME: a statement fixed at a moment inside the
            // ring is a keyframe. Refused rather than read, like every removed spelling — nothing
            // in the tree writes the old word.
            .remedy = "re-import the package to get \"keyframes\"",
        },
        RemovedSpelling{
            .key = "bend",
            .was = [](const juce::var& v) { return v.isArray(); },
            // There is no bend CURVE array: the onset value is this key as a number, and every
            // later value is a keyframe's bend channel.
            .remedy = R"(re-import the package to get the onset "bend" value and "keyframes")",
        },
        RemovedSpelling{
            .key = "vibrato",
            .was = [](const juce::var& v) { return v.isBool(); },
            // Vibrato is a width AXIS: the ordinary vibrato is `"narrow"` and the deliberate
            // exaggeration `"wide"`, which one bool could not tell apart.
            .remedy = R"(re-import the package to get "vibrato": "narrow")",
        },
        RemovedSpelling{
            .key = "slideOut",
            .was = [](const juce::var& v) { return v.isObject() || v.isInt(); },
            // The slide-out is the keyframe at the ring's end (chart.h): a fret stated where the
            // sound stops is where the hand leaves toward, so the gesture needs no field of its
            // own.
            .remedy =
                "re-import the package to get the slide-out as the keyframe at the ring's end",
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
    // keyframe objects carry the same rule in \ref readKeyframe, per channel, because absence is a
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
        ScalarRule{.key = "vibrato", .matches = [](const juce::var& v) { return v.isString(); }},
        ScalarRule{
            .key = "bend", .matches = [](const juce::var& v) { return v.isDouble() || v.isInt(); }
        },
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

    // The ring. Required with no default: a note rings for some length, so a missing key is a
    // missing fact rather than "no tail", and reading it as zero would silently invent the one
    // datum the model cannot derive. A document missing the key is out of date rather than
    // merely short, so the message carries the re-import remedy exactly like the removed-key
    // tripwires above.
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

    note.palm_mute = Json::readOptionalBool(note_json, "palmMute", false);
    note.dead = Json::readOptionalBool(note_json, "dead", false);
    note.harmonic_node = Json::tryReadDouble(note_json, "harmonicNode");

    // The first leg's width: absence already says it does not vibrate — the same rule the absent
    // pick attack and the absent `normal` emphasis follow.
    auto vibrato = readVibrato(note_json, "note");
    if (!vibrato.has_value())
    {
        return std::unexpected{std::move(vibrato.error())};
    }
    note.vibrato = *vibrato;
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

    if (const juce::var& keyframes_json = Json::value(note_json, "keyframes");
        !keyframes_json.isVoid())
    {
        if (!keyframes_json.isArray())
        {
            return std::unexpected{malformed("chart note keyframes must be an array")};
        }
        note.keyframes.reserve(static_cast<std::size_t>(keyframes_json.size()));
        for (int index = 0; index < keyframes_json.size(); ++index)
        {
            const juce::var& keyframe_json = keyframes_json[index];
            auto keyframe = readKeyframe(keyframe_json);
            if (!keyframe.has_value())
            {
                return std::unexpected{std::move(keyframe.error())};
            }
            note.keyframes.push_back(*keyframe);
        }
    }

    return note;
}

// The one spelling of the no-arpeggio grip, read and written. An unnamed grip is the absent key.
constexpr std::string_view g_no_arpeggio_token{"none"};

// One shape hold: both edges required, the grip optional. A present grip must be the one token,
// since a wrong-typed or unknown one read as absent would turn a stated no-arpeggio into a grip.
[[nodiscard]] std::expected<ShapeHold, ChartError> readShapeHold(
    const juce::var& hold_json, const TempoMap& tempo_map)
{
    auto start = readPlace(hold_json, "at", tempo_map);
    if (!start.has_value())
    {
        return std::unexpected{std::move(start.error())};
    }
    auto end = readPlace(hold_json, "end", tempo_map);
    if (!end.has_value())
    {
        return std::unexpected{std::move(end.error())};
    }
    HoldGrip grip = UnnamedGrip{};
    if (const juce::var& grip_json = Json::value(hold_json, "grip"); !grip_json.isVoid())
    {
        const std::string token = grip_json.isString() ? grip_json.toString().toStdString() : "";
        if (token != g_no_arpeggio_token)
        {
            return std::unexpected{malformed("chart hold \"grip\" is not a known grip: " + token)};
        }
        grip = NoArpeggio{};
    }
    return ShapeHold{.start = *start, .end = *end, .grip = grip};
}

// ---- writer -------------------------------------------------------------------------------

void appendJsonString(std::string& out, const std::string& text)
{
    out += juce::JSON::toString(juce::var{juce::String{text}}, true).toStdString();
}

// A chart's doubles are measurements — a harmonic node is `12 * log2(partial)`, a bend height a
// fraction of a step — so the writer needs the shared round-trip-exact form. `juce::String{double}`
// is NOT that: it writes the stream's default six significant digits, which silently rounds every
// node and semitone, and can round an out-of-range value back into range on the way out.
[[nodiscard]] std::string doubleText(double value)
{
    return Json::numberText(value);
}

[[nodiscard]] std::string noteLine(const ChartNote& note, const TempoMap& tempo_map)
{
    std::string line = R"({ "at": ")" + formatPlaceToken(tempo_map, note.position) + '"';
    line += ", \"string\": " + std::to_string(note.string);
    line += ", \"fret\": " + std::to_string(note.fret);
    // Always emitted: the ring is the note's own fact, and the reader refuses a document that omits
    // it, so there is no shorter form to elide into.
    line += R"(, "sustain": ")" + formatFractionToken(note.sustain) + '"';
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
    // `None` is the absence rather than a word, so the unvibrated leg costs nothing — one spelling
    // for no vibrato, the same elision every defaulted note property takes.
    if (hasVibrato(note.vibrato))
    {
        line += R"(, "vibrato": ")" + std::string{vibratoToken(note.vibrato)} + '"';
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
    if (!note.keyframes.empty())
    {
        line += R"(, "keyframes": [)";
        for (std::size_t index = 0; index < note.keyframes.size(); ++index)
        {
            const Keyframe& keyframe = note.keyframes[index];
            if (index > 0)
            {
                line += ", ";
            }
            line += R"({ "offset": ")" + formatFractionToken(keyframe.offset) + '"';
            // Every STATED channel is written, values that look like defaults included: a bend of
            // zero is a release back to rest, so eliding it would delete the statement rather than
            // shorten it. Absence is what says nothing was stated — for the vibrato, `None`.
            //
            // Each channel is bound to a local so its check and its access are provably the same
            // object, which the CI-only optional-access checker does not credit across two
            // separate reads of an indexed element.
            const std::optional<int>& fret = keyframe.fret;
            if (fret.has_value())
            {
                line += R"(, "fret": )" + std::to_string(*fret);
            }
            const std::optional<double>& bend = keyframe.bend;
            if (bend.has_value())
            {
                line += R"(, "bend": )" + doubleText(*bend);
            }
            if (hasVibrato(keyframe.vibrato))
            {
                line += R"(, "vibrato": ")" + std::string{vibratoToken(keyframe.vibrato)} + '"';
            }
            line += " }";
        }
        line += ']';
    }
    line += " }";
    return line;
}

// The document's root through the single chart-document version gate, mirroring the other formats:
// g_chart_format_version is the only supported revision, and no other call site may test the chart
// version. The format itself changes in place under this number — every project is fresh, nothing
// legacy is preserved, and stale documents are simply re-imported.
[[nodiscard]] std::expected<juce::var, ChartError> parseChartRoot(const std::string& text)
{
    auto document = Json::parseUtf8Document(text);
    if (!document.has_value())
    {
        return std::unexpected{malformed("chart document is not valid JSON")};
    }
    if (Json::readOptionalInt(*document, "formatVersion", 0) != g_chart_format_version)
    {
        return std::unexpected{malformed(
            "chart document formatVersion must be " + std::to_string(g_chart_format_version))};
    }
    return std::move(*document);
}

[[nodiscard]] std::expected<ChartTuning, ChartError> readTuning(const juce::var& root)
{
    ChartTuning tuning;
    const juce::var& tuning_json = Json::value(root, "tuning");
    const juce::var& strings_json = Json::value(tuning_json, "strings");
    if (!strings_json.isArray())
    {
        return std::unexpected{malformed("chart tuning strings must be an array")};
    }
    tuning.strings.reserve(static_cast<std::size_t>(strings_json.size()));
    for (int index = 0; index < strings_json.size(); ++index)
    {
        tuning.strings.push_back(strings_json[index].toString().toStdString());
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
    tuning.capo = Json::readOptionalInt(tuning_json, "capo", 0);
    tuning.cent_offset = Json::readOptionalDouble(tuning_json, "centOffset", 0.0);
    return tuning;
}

} // namespace

std::expected<ChartTuning, ChartError> parseChartDocumentTuning(const std::string& text)
{
    const auto root = parseChartRoot(text);
    if (!root.has_value())
    {
        return std::unexpected{root.error()};
    }
    return readTuning(*root);
}

std::expected<Chart, ChartError> parseChartDocument(
    const std::string& text, const TempoMap& tempo_map)
{
    const auto document = parseChartRoot(text);
    if (!document.has_value())
    {
        return std::unexpected{document.error()};
    }
    const juce::var& root = *document;

    Chart chart;
    auto tuning = readTuning(root);
    if (!tuning.has_value())
    {
        return std::unexpected{std::move(tuning.error())};
    }
    chart.tuning = std::move(*tuning);

    // No posture table or span list is stored: both are derived from the notes (deriveChartShapes),
    // so a document carrying them states a second, unverifiable copy of something the notes
    // already say. Refused rather than ignored, the same tripwire the removed note fields get, so
    // an out-of-date package fails loudly with the fix named instead of loading with a stale
    // picture silently discarded. Delete this once no package carries those fields — it exists to
    // fail loudly, not to support the old shape.
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
            auto note = readNote(notes_json[index], tempo_map);
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

    const juce::var& positions_json = Json::value(root, "positions");
    if (positions_json.isArray())
    {
        chart.fret_hand_positions.reserve(static_cast<std::size_t>(positions_json.size()));
        for (int index = 0; index < positions_json.size(); ++index)
        {
            const juce::var& fhp_json = positions_json[index];
            auto position = readPlace(fhp_json, "at", tempo_map);
            if (!position.has_value())
            {
                return std::unexpected{std::move(position.error())};
            }
            // The fret is the whole of what a placement states, so it is required with no default,
            // as a note's ring is. A note's missing fret reads as a sentinel the validator
            // refuses, but a placement's fret passes the normalizer first, which would lift any
            // sentinel onto the board and invent the placement; so a missing one is refused here.
            const juce::var& fret_json = Json::value(fhp_json, "fret");
            if (fret_json.isVoid())
            {
                return std::unexpected{malformed("chart hand position is missing \"fret\"")};
            }
            if (!fret_json.isInt())
            {
                return std::unexpected{malformed(
                    "chart hand position \"fret\" has the wrong type")};
            }
            // The end fret is optional: absent, the notes derive the window's reach.
            const juce::var& end_fret_json = Json::value(fhp_json, "endFret");
            if (!end_fret_json.isVoid() && !end_fret_json.isInt())
            {
                return std::unexpected{malformed(
                    "chart hand position \"endFret\" has the wrong type")};
            }
            chart.fret_hand_positions.push_back(
                FretHandPosition{
                    .position = *position,
                    .fret = Json::readOptionalInt(fhp_json, "fret", 0),
                    .end_fret = end_fret_json.isVoid()
                                    ? std::nullopt
                                    : std::optional{Json::readOptionalInt(fhp_json, "endFret", 0)},
                });
        }
    }

    const juce::var& holds_json = Json::value(root, "holds");
    if (holds_json.isArray())
    {
        chart.shape_holds.reserve(static_cast<std::size_t>(holds_json.size()));
        for (int index = 0; index < holds_json.size(); ++index)
        {
            auto hold = readShapeHold(holds_json[index], tempo_map);
            if (!hold.has_value())
            {
                return std::unexpected{std::move(hold.error())};
            }
            chart.shape_holds.push_back(*hold);
        }
    }

    return chart;
}

std::expected<Chart, ChartError> readChartDocument(
    const std::filesystem::path& file, const TempoMap& tempo_map)
{
    const juce::File chart_file = juceFileFromPath(file);
    if (!chart_file.existsAsFile())
    {
        return std::unexpected{malformed("chart document does not exist: " + file.string())};
    }
    return parseChartDocument(chart_file.loadFileAsString().toStdString(), tempo_map);
}

namespace
{

// Renders a chart already in its document form (documentChart): every note in saved form, every
// claim settled. Nothing here converts but the addresses, which the file holds as whole-note
// places, so the text is exactly the chart it is handed.
[[nodiscard]] std::string renderChartDocument(const Chart& chart, const TempoMap& tempo_map)
{
    std::string text = "{\n  \"formatVersion\": " + std::to_string(g_chart_format_version) + ",\n";

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

    append_array("notes", chart.notes, [&tempo_map](const ChartNote& note) {
        return noteLine(note, tempo_map);
    });
    append_array("positions", chart.fret_hand_positions, [&tempo_map](const FretHandPosition& fhp) {
        std::string line = R"({ "at": ")" + formatPlaceToken(tempo_map, fhp.position) +
                           R"(", "fret": )" + std::to_string(fhp.fret);
        // Written only where authored: an absent end is the derived window.
        if (const std::optional<int>& end_fret = fhp.end_fret; end_fret.has_value())
        {
            line += R"(, "endFret": )" + std::to_string(*end_fret);
        }
        return line + " }";
    });
    append_array("holds", chart.shape_holds, [&tempo_map](const ShapeHold& hold) {
        std::string line = R"({ "at": ")" + formatPlaceToken(tempo_map, hold.start) +
                           R"(", "end": ")" + formatPlaceToken(tempo_map, hold.end) + '"';
        if (std::holds_alternative<NoArpeggio>(hold.grip))
        {
            line += R"(, "grip": ")" + std::string{g_no_arpeggio_token} + '"';
        }
        return line + " }";
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
        // THE KEYFRAME COMMIT LAW's writer half: a point that says nothing the path does not
        // already say is authoring state, never document (keyframeSaysNothingNew).
        static_cast<void>(stripSilentKeyframes(note));
    }
    return document;
}

std::string chartDocumentText(const Chart& chart, const TempoMap& tempo_map)
{
    return renderChartDocument(documentChart(chart, tempo_map), tempo_map);
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
    if (!chart_file.replaceWithText(
            juce::String::fromUTF8(renderChartDocument(document, tempo_map).c_str())))
    {
        return std::unexpected{malformed("could not write the chart document: " + file.string())};
    }
    return {};
}

} // namespace rock_hero::common::core
