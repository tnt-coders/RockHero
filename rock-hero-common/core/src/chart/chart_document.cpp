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

// The vibrato channel's tokens, in both directions. One table so the reader and the writer cannot
// disagree about a spelling, and so the axis's off value has exactly one word wherever it is
// legal to write at all (a keyframe, never an onset).
constexpr std::array<std::pair<std::string_view, VibratoState>, 3> g_vibrato_tokens{{
    {"off", VibratoState::Off},
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
    // Total above; a value outside the axis is a caller bug, and spelling it as any of the three
    // would write a width nobody authored.
    std::unreachable();
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
        // spellings, which nested channels had no equivalent of until the vibrato channel stopped
        // being a bool. Null where a channel has never changed shape, and then a wrong-typed value
        // reports the plain type message it always did.
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
            // The shake became an AXIS with a width: a bool could say only that the string shook,
            // and never how wide.
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
    // Every width is a statement HERE, `off` included: the channel holds until restated, so a
    // keyframe is the only place the chart can say the shake ends. Unlike the onset, which has no
    // word for off at all.
    std::optional<VibratoState> vibrato;
    if (!Json::value(keyframe_json, "vibrato").isVoid())
    {
        const std::string token = Json::readOptionalString(keyframe_json, "vibrato", "");
        vibrato = parseVibratoToken(token);
        if (!vibrato.has_value())
        {
            return std::unexpected{malformed("chart keyframe vibrato is unknown: " + token)};
        }
    }
    // A keyframe stating no channel at all is refused by validateChartNoteAlone rather than here:
    // this reader answers what the document SAYS, and an empty statement is a legality question
    // the one rules authority owns.
    return Keyframe{
        .offset = *offset,
        .fret = Json::value(keyframe_json, "fret").isVoid()
                    ? std::nullopt
                    : std::optional{Json::readOptionalInt(keyframe_json, "fret", 0)},
        .bend = Json::value(keyframe_json, "bend").isVoid()
                    ? std::nullopt
                    : std::optional{Json::readOptionalDouble(keyframe_json, "bend", 0.0)},
        .vibrato = vibrato,
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
    // predates a change reports the re-import remedy instead of a bare "wrong type" — two of
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
            .remedy = R"(re-import the package to get "palmMute" and "dead")",
        },
        RemovedSpelling{
            .key = "harmonic",
            .was = [](const juce::var&) { return true; },
            // The harmonic field is gone: a node asserts the harmonic and `attack` says which
            // hand damps it.
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
            // The accent bool became one end of the emphasis axis, whose other end is the ghost.
            .remedy = R"(re-import the package to get "emphasis": "accent")",
        },
        RemovedSpelling{
            .key = "slides",
            .was = [](const juce::var&) { return true; },
            // Slide keyframes became the one interval-payload array, which every channel shares.
            .remedy = "re-import the package to get \"keyframes\"",
        },
        RemovedSpelling{
            .key = "waypoints",
            .was = [](const juce::var&) { return true; },
            // The array is unchanged in shape and only its NAME moved: a statement fixed at a
            // moment inside the ring is a keyframe. Refused rather than read, for the reason
            // every removed spelling is — a document saying the old word is a document nothing
            // in the tree writes any more.
            .remedy = "re-import the package to get \"keyframes\"",
        },
        RemovedSpelling{
            .key = "bend",
            .was = [](const juce::var& v) { return v.isArray(); },
            // The bend CURVE dissolved: its onset value is this key as a number, and every later
            // value is a keyframe's bend channel.
            .remedy = R"(re-import the package to get the onset "bend" value and "keyframes")",
        },
        RemovedSpelling{
            .key = "vibrato",
            .was = [](const juce::var& v) { return v.isBool(); },
            // The shake bool became the width AXIS: the ordinary vibrato is `"narrow"` and the
            // deliberate exaggeration `"wide"`, which one bool could not tell apart.
            .remedy = R"(re-import the package to get "vibrato": "narrow")",
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
        ScalarRule{.key = "held", .matches = [](const juce::var& v) { return v.isInt(); }},
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
    else if (attack == "none")
    {
        note.attack = NoteAttack::None;
    }
    else if (!attack.empty())
    {
        return std::unexpected{malformed("chart note attack is unknown: " + attack)};
    }

    // The ring, read AFTER the attack because the attack decides whether there is one to read.
    // Required with no default on every attack that sounds: a note rings for some length, so a
    // missing key is a missing fact rather than "no tail", and reading it as zero would silently
    // invent the one datum the model cannot derive. This is also the path a chart written before
    // the duration model actually takes, and the only one: that writer OMITTED the key on every
    // tail-less note, which is most of them, so the message carries the re-import remedy exactly
    // like the removed-key tripwires above.
    //
    // A silent hold has no ring at all, so the key must be ABSENT rather than zero — the same
    // posture every defaulted property takes, applied to the one property whose default depends on
    // the attack. A written zero is refused instead of accepted as the value the reader cannot
    // tell from an absent key, so the document has exactly one spelling for "no ring".
    if (silentHold(note.attack))
    {
        if (!Json::value(note_json, "sustain").isVoid())
        {
            return std::unexpected{malformed(
                "chart note states \"sustain\" on a silently held stop, which has no ring of its "
                "own")};
        }
    }
    else
    {
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
    }

    // The fretting hand's stop under a right-hand onset. Absence is a MEANING here — the hand
    // states no stop of its own — so presence is the whole read and there is no default to fall
    // back to; the out-of-range sentinel the lenient reader needs cannot arise, because the key's
    // type was checked above and every integer it can hold is judged by the rules.
    if (!Json::value(note_json, "held").isVoid())
    {
        note.held = Json::readOptionalInt(note_json, "held", -1);
    }

    note.palm_mute = Json::readOptionalBool(note_json, "palmMute", false);
    note.dead = Json::readOptionalBool(note_json, "dead", false);
    note.harmonic_node = Json::tryReadDouble(note_json, "harmonicNode");

    // The channel's opening width. Present means it must name a WIDTH: `off` is refused along with
    // anything unknown, because absence already says a note does not shake — the same rule the
    // absent pick attack and the absent `normal` emphasis follow, and the writer can never produce
    // the token. A keyframe is where the shake ENDS, and there all three words are legal.
    if (!Json::value(note_json, "vibrato").isVoid())
    {
        const std::string vibrato = Json::readOptionalString(note_json, "vibrato", "");
        // An unknown word and the one word that is legal only on a keyframe collapse to the same
        // refusal, which is why this reads the WIDTH rather than the optional: both are a document
        // saying something an onset cannot say.
        const VibratoState parsed = parseVibratoToken(vibrato).value_or(VibratoState::Off);
        if (!isShaking(parsed))
        {
            return std::unexpected{malformed("chart note vibrato is unknown: " + vibrato)};
        }
        note.vibrato = parsed;
    }
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

    // The gestured fret alone: a slide-out releases off the note's END, so its moment is the ring's
    // and it stores none of its own (chart.h). Pitched glides — shift and legato alike — are the
    // fret channel of ordinary keyframes above.
    if (!Json::value(note_json, "slideOut").isVoid())
    {
        note.slide_out = Json::readOptionalInt(note_json, "slideOut", -1);
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
    // Emitted for every attack that sounds: the ring is such a note's own fact, and the reader
    // refuses a document that omits it, so there is no shorter form to elide into. A silent hold
    // has no ring, and the reader refuses the key there in the other direction — one spelling
    // each way.
    if (!silentHold(note.attack))
    {
        line += R"(, "sustain": ")" + formatBeatFractionToken(note.sustain) + '"';
    }
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
        case NoteAttack::None:
        {
            line += R"(, "attack": "none")";
            break;
        }
    }
    // Elided when the hand states no stop of its own, because absence is that meaning rather than
    // a shorter spelling of some default: fret 0 is the open string a voicing deliberately leaves,
    // and it is written like any other stop. Bound to a local so the optional check and the access
    // are provably the same object.
    if (const std::optional<int>& held = note.held; held.has_value())
    {
        line += R"(, "held": )" + std::to_string(*held);
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
    // Off is the onset's absence rather than a word, so the un-shaken note costs nothing and the
    // reader can refuse `"off"` here outright — one spelling for not shaking, the same elision
    // every defaulted note property takes.
    if (isShaking(note.vibrato))
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
            line += R"({ "offset": ")" + formatBeatFractionToken(keyframe.offset) + '"';
            // Every STATED channel is written, values that look like defaults included: a bend of
            // zero is a release back to rest and an `"off"` vibrato is a shake ENDING, so eliding
            // either would delete the statement rather than shorten it. Absence is what says
            // nothing was stated.
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
            const std::optional<VibratoState>& vibrato = keyframe.vibrato;
            if (vibrato.has_value())
            {
                line += R"(, "vibrato": ")" + std::string{vibratoToken(*vibrato)} + '"';
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
    // The silently-held member moved into the note stream as an attack, so its own array is gone.
    // Refused rather than ignored, for the reason every removed spelling is: a document carrying
    // it would load with every hold silently missing and validate clean.
    if (!Json::value(root, "holdMarkers").isVoid())
    {
        return std::unexpected{malformed(
            "chart uses the removed \"holdMarkers\" field; re-import the package to get silently "
            "held stops as notes with \"attack\": \"none\"")};
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
