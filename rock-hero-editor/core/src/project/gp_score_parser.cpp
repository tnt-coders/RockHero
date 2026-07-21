#include "project/gp_score_parser.h"

#include <cstddef>
#include <juce_core/juce_core.h>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace rock_hero::editor::core
{

namespace
{

using common::core::Fraction;

// Every parser rejection means the same thing to the import boundary: the score data cannot
// become a valid song. This wraps the message in the importer's typed error.
[[nodiscard]] std::unexpected<SongImportError> invalidScore(std::string message)
{
    return std::unexpected{
        SongImportError{SongImportErrorCode::InvalidImportedSong, std::move(message)}
    };
}

// The chart's Fraction is a value type without arithmetic; rhythm math stays in tiny rationals.
[[nodiscard]] constexpr Fraction multiplyFractions(Fraction lhs, Fraction rhs) noexcept
{
    return Fraction{lhs.numerator * rhs.numerator, lhs.denominator * rhs.denominator};
}

// Guitar Pro stores sync-point frame offsets at a fixed 44.1kHz frame rate regardless of the
// embedded asset's actual sample rate (verified against corpus files whose assets are 48kHz:
// only 44100 reproduces the sync points' own ModifiedTempo).
constexpr double g_sync_frame_rate{44100.0};

// Returns the trimmed text content of a child element, or an empty string.
[[nodiscard]] std::string childText(const juce::XmlElement& element, const char* child_name)
{
    const juce::XmlElement* const child = element.getChildByName(child_name);
    if (child == nullptr)
    {
        return {};
    }

    return child->getAllSubText().trim().toStdString();
}

// Returns a child's text as an integer, or the fallback when absent or unparsable.
[[nodiscard]] int childInt(const juce::XmlElement& element, const char* child_name, int fallback)
{
    const std::string text = childText(element, child_name);
    if (text.empty())
    {
        return fallback;
    }

    return juce::String{text}.getIntValue();
}

// Returns a child's text as a double, or the fallback when absent or unparsable.
[[nodiscard]] double childDouble(
    const juce::XmlElement& element, const char* child_name, double fallback)
{
    const std::string text = childText(element, child_name);
    if (text.empty())
    {
        return fallback;
    }

    return juce::String{text}.getDoubleValue();
}

// Builds an id-keyed table over one of the gpif entity list elements ("Bars", "Beats", ...).
[[nodiscard]] std::map<int, const juce::XmlElement*> idTable(
    const juce::XmlElement& root, const char* list_name)
{
    std::map<int, const juce::XmlElement*> table;
    const juce::XmlElement* const list = root.getChildByName(list_name);
    if (list == nullptr)
    {
        return table;
    }

    for (const auto* entry : list->getChildIterator())
    {
        table[entry->getIntAttribute("id", -1)] = entry;
    }

    return table;
}

// Splits a whitespace-separated id list ("0 3 -1 7") into integers.
[[nodiscard]] std::vector<int> idList(const std::string& text)
{
    std::vector<int> ids;
    juce::StringArray tokens;
    tokens.addTokens(juce::String{text}, false);
    for (const juce::String& token : tokens)
    {
        if (token.isNotEmpty())
        {
            ids.push_back(token.getIntValue());
        }
    }

    return ids;
}

// Finds a Properties/Property child by name, or null.
[[nodiscard]] const juce::XmlElement* findProperty(
    const juce::XmlElement& element, const char* property_name)
{
    const juce::XmlElement* const properties = element.getChildByName("Properties");
    if (properties == nullptr)
    {
        return nullptr;
    }

    for (const auto* property : properties->getChildIterator())
    {
        if (property->getStringAttribute("name") == property_name)
        {
            return property;
        }
    }

    return nullptr;
}

// Converts a rhythm element to its duration as a fraction of a whole note.
[[nodiscard]] std::expected<Fraction, SongImportError> rhythmDuration(
    const juce::XmlElement& rhythm)
{
    const std::string value = childText(rhythm, "NoteValue");
    Fraction duration{1, 4};
    if (value == "Whole")
    {
        duration = Fraction{1};
    }
    else if (value == "Half")
    {
        duration = Fraction{1, 2};
    }
    else if (value == "Quarter")
    {
        duration = Fraction{1, 4};
    }
    else if (value == "Eighth")
    {
        duration = Fraction{1, 8};
    }
    else if (value == "16th")
    {
        duration = Fraction{1, 16};
    }
    else if (value == "32nd")
    {
        duration = Fraction{1, 32};
    }
    else if (value == "64th")
    {
        duration = Fraction{1, 64};
    }
    else
    {
        return invalidScore("unknown rhythm value: " + value);
    }

    if (const juce::XmlElement* const dot = rhythm.getChildByName("AugmentationDot");
        dot != nullptr)
    {
        const int dots = dot->getIntAttribute("count", 0);
        // One dot multiplies by 3/2, two by 7/4: (2^(n+1) - 1) / 2^n.
        const int numerator = (1 << (dots + 1)) - 1;
        duration = multiplyFractions(duration, Fraction{numerator, 1 << dots});
    }

    if (const juce::XmlElement* const tuplet = rhythm.getChildByName("PrimaryTuplet");
        tuplet != nullptr)
    {
        const int num = tuplet->getIntAttribute("num", 1);
        const int den = tuplet->getIntAttribute("den", 1);
        if (num <= 0 || den <= 0)
        {
            return invalidScore("invalid tuplet on rhythm");
        }
        duration = multiplyFractions(duration, Fraction{den, num});
    }

    return duration;
}

// Parses one note element with its technique fields.
[[nodiscard]] GpNote parseNote(const juce::XmlElement& note_element)
{
    GpNote note;
    if (const juce::XmlElement* const property = findProperty(note_element, "String");
        property != nullptr)
    {
        note.string = childInt(*property, "String", 0);
    }
    if (const juce::XmlElement* const property = findProperty(note_element, "Fret");
        property != nullptr)
    {
        note.fret = childInt(*property, "Fret", 0);
    }

    if (const juce::XmlElement* const tie = note_element.getChildByName("Tie"); tie != nullptr)
    {
        note.tie_origin = tie->getStringAttribute("origin") == "true";
        note.tie_destination = tie->getStringAttribute("destination") == "true";
    }

    note.hopo_destination = findProperty(note_element, "HopoDestination") != nullptr;
    note.tapped = findProperty(note_element, "Tapped") != nullptr ||
                  findProperty(note_element, "LeftHandTapped") != nullptr;
    note.palm_mute = findProperty(note_element, "PalmMuted") != nullptr;
    note.full_mute = findProperty(note_element, "Muted") != nullptr;
    note.vibrato = note_element.getChildByName("Vibrato") != nullptr;

    // Accent is a bitset: 1 = staccato, 4 = heavy accent, 8 = accent. Staccato alone is not an
    // accent in the chart's sense.
    const std::string accent_text = childText(note_element, "Accent");
    if (!accent_text.empty())
    {
        const int accent_flags = juce::String{accent_text}.getIntValue();
        note.accent = (accent_flags & (4 | 8)) != 0;
    }

    if (const juce::XmlElement* const slide = findProperty(note_element, "Slide"); slide != nullptr)
    {
        note.slide_flags = childInt(*slide, "Flags", 0);
    }

    if (const juce::XmlElement* const harmonic_type = findProperty(note_element, "HarmonicType");
        harmonic_type != nullptr)
    {
        note.harmonic_type = childText(*harmonic_type, "HType");
    }
    if (const juce::XmlElement* const harmonic_fret = findProperty(note_element, "HarmonicFret");
        harmonic_fret != nullptr)
    {
        note.harmonic_fret = childDouble(*harmonic_fret, "HFret", 0.0);
    }

    if (findProperty(note_element, "Bended") != nullptr)
    {
        GpBend bend;
        const auto bend_value = [&](const char* name, double fallback) {
            const juce::XmlElement* const property = findProperty(note_element, name);
            return property == nullptr ? fallback : childDouble(*property, "Float", fallback);
        };
        bend.origin_value = bend_value("BendOriginValue", 0.0);
        bend.middle_value = bend_value("BendMiddleValue", 0.0);
        bend.destination_value = bend_value("BendDestinationValue", 0.0);
        bend.origin_offset = bend_value("BendOriginOffset", 0.0);
        bend.middle_offset1 = bend_value("BendMiddleOffset1", 50.0);
        // The middle value holds between the two middle offsets; a missing second offset means
        // the plateau collapses to the single middle point.
        bend.middle_offset2 = bend_value("BendMiddleOffset2", bend.middle_offset1);
        bend.destination_offset = bend_value("BendDestinationOffset", 100.0);
        note.bend = bend;
    }

    return note;
}

// Parses the master-track automations into the base tempo and the audio sync points.
void parseAutomations(const juce::XmlElement& root, GpScore& score)
{
    const juce::XmlElement* const master_track = root.getChildByName("MasterTrack");
    const juce::XmlElement* const automations =
        master_track == nullptr ? nullptr : master_track->getChildByName("Automations");
    if (automations == nullptr)
    {
        return;
    }

    for (const auto* automation : automations->getChildIterator())
    {
        const std::string type = childText(*automation, "Type");
        if (type == "Tempo")
        {
            // Tempo values are "<bpm> <unit>" where the unit scales the beat the BPM counts:
            // 1 = eighth, 2 = quarter, 3 = dotted quarter, 4 = half, 5 = dotted half.
            const juce::StringArray parts =
                juce::StringArray::fromTokens(juce::String{childText(*automation, "Value")}, true);
            if (parts.size() >= 1 && childInt(*automation, "Bar", 0) == 0)
            {
                double bpm = parts[0].getDoubleValue();
                const int unit = parts.size() >= 2 ? parts[1].getIntValue() : 2;
                switch (unit)
                {
                    case 1:
                        bpm *= 0.5;
                        break;
                    case 3:
                        bpm *= 1.5;
                        break;
                    case 4:
                        bpm *= 2.0;
                        break;
                    case 5:
                        bpm *= 3.0;
                        break;
                    default:
                        break;
                }
                if (bpm > 0.0)
                {
                    score.base_tempo_quarter_bpm = bpm;
                }
            }
            continue;
        }
        if (type != "SyncPoint")
        {
            continue;
        }

        const juce::XmlElement* const value = automation->getChildByName("Value");
        if (value == nullptr)
        {
            continue;
        }

        GpSyncPoint sync;
        sync.bar = childInt(*value, "BarIndex", childInt(*automation, "Bar", 0));
        sync.bar_fraction = juce::String{childText(*automation, "Position")}.getDoubleValue();
        sync.modified_tempo = childDouble(*value, "ModifiedTempo", 0.0);
        sync.seconds = childDouble(*value, "FrameOffset", 0.0) / g_sync_frame_rate;
        score.sync_points.push_back(sync);
    }
}

} // namespace

std::expected<GpScore, SongImportError> parseGpScore(const std::string& gpif_xml)
{
    const std::unique_ptr<juce::XmlElement> root = juce::XmlDocument::parse(
        juce::String::fromUTF8(gpif_xml.data(), static_cast<int>(gpif_xml.size())));
    if (root == nullptr || !root->hasTagName("GPIF"))
    {
        return invalidScore("score.gpif is not a GPIF XML document");
    }

    GpScore score;
    if (const juce::XmlElement* const score_element = root->getChildByName("Score");
        score_element != nullptr)
    {
        score.title = childText(*score_element, "Title");
        score.artist = childText(*score_element, "Artist");
        score.album = childText(*score_element, "Album");
    }

    parseAutomations(*root, score);

    // The backing asset referenced by the BackingTrack, resolved through the asset table.
    if (const juce::XmlElement* const backing = root->getChildByName("BackingTrack");
        backing != nullptr)
    {
        // The audio's signed placement on the score timeline: positive is silence before the
        // audio, negative pulls the recording's head before the score start. The importer turns
        // it into the signed asset start offset. Always in 44.1kHz frames regardless of the
        // asset's real sample rate.
        score.frame_padding = childInt(*backing, "FramePadding", 0);

        const std::string asset_id = childText(*backing, "AssetId");
        if (const juce::XmlElement* const assets = root->getChildByName("Assets");
            assets != nullptr && !asset_id.empty())
        {
            for (const auto* asset : assets->getChildIterator())
            {
                if (asset->getStringAttribute("id").toStdString() == asset_id)
                {
                    score.embedded_audio_entry = childText(*asset, "EmbeddedFilePath");
                }
            }
        }
    }

    const auto bars_table = idTable(*root, "Bars");
    const auto voices_table = idTable(*root, "Voices");
    const auto beats_table = idTable(*root, "Beats");
    const auto notes_table = idTable(*root, "Notes");
    const auto rhythms_table = idTable(*root, "Rhythms");

    const juce::XmlElement* const master_bars = root->getChildByName("MasterBars");
    const juce::XmlElement* const tracks = root->getChildByName("Tracks");
    if (master_bars == nullptr || tracks == nullptr)
    {
        return invalidScore("score.gpif is missing MasterBars or Tracks");
    }

    // Reject the linearization features the chart format cannot represent.
    for (const auto* master_bar : master_bars->getChildIterator())
    {
        if (master_bar->getChildByName("Repeat") != nullptr ||
            master_bar->getChildByName("AlternateEndings") != nullptr ||
            master_bar->getChildByName("Directions") != nullptr)
        {
            return invalidScore(
                "score uses repeats or jump directions; export it linearly and re-import");
        }
    }

    // Track headers: name, tuning, capo.
    for (const auto* track_element : tracks->getChildIterator())
    {
        GpTrack track;
        track.name = childText(*track_element, "Name");
        if (const juce::XmlElement* const staves = track_element->getChildByName("Staves");
            staves != nullptr && staves->getFirstChildElement() != nullptr)
        {
            const juce::XmlElement& staff = *staves->getFirstChildElement();
            if (const juce::XmlElement* const tuning = findProperty(staff, "Tuning");
                tuning != nullptr)
            {
                for (const int midi : idList(childText(*tuning, "Pitches")))
                {
                    track.tuning_midi.push_back(midi);
                }
            }
            if (const juce::XmlElement* const capo = findProperty(staff, "CapoFret");
                capo != nullptr)
            {
                track.capo = childInt(*capo, "Fret", 0);
            }
        }
        if (track.tuning_midi.empty())
        {
            return invalidScore("track has no string tuning: " + track.name);
        }
        score.tracks.push_back(std::move(track));
    }

    // Master bars and each track's bar content.
    for (const auto* master_bar : master_bars->getChildIterator())
    {
        GpMasterBar bar;
        const std::string time = childText(*master_bar, "Time");
        const auto slash = time.find('/');
        if (slash == std::string::npos)
        {
            return invalidScore("master bar has no time signature");
        }
        bar.numerator = juce::String{time.substr(0, slash)}.getIntValue();
        bar.denominator = juce::String{time.substr(slash + 1)}.getIntValue();
        if (bar.numerator <= 0 || bar.denominator <= 0)
        {
            return invalidScore("master bar time signature is invalid: " + time);
        }
        if (const juce::XmlElement* const section = master_bar->getChildByName("Section");
            section != nullptr)
        {
            bar.section = childText(*section, "Text");
            if (bar.section.empty())
            {
                bar.section = childText(*section, "Letter");
            }
        }

        const std::vector<int> bar_ids = idList(childText(*master_bar, "Bars"));
        if (bar_ids.size() != score.tracks.size())
        {
            return invalidScore("master bar does not reference every track");
        }

        for (std::size_t track_index = 0; track_index < bar_ids.size(); ++track_index)
        {
            GpBar track_bar;
            const auto bar_entry = bars_table.find(bar_ids[track_index]);
            if (bar_entry == bars_table.end())
            {
                return invalidScore("master bar references a missing bar");
            }

            for (const int voice_id : idList(childText(*bar_entry->second, "Voices")))
            {
                if (voice_id < 0)
                {
                    continue;
                }
                const auto voice_entry = voices_table.find(voice_id);
                if (voice_entry == voices_table.end())
                {
                    return invalidScore("bar references a missing voice");
                }

                std::vector<GpBeat> beats;
                for (const int beat_id : idList(childText(*voice_entry->second, "Beats")))
                {
                    const auto beat_entry = beats_table.find(beat_id);
                    if (beat_entry == beats_table.end())
                    {
                        return invalidScore("voice references a missing beat");
                    }
                    const juce::XmlElement& beat_element = *beat_entry->second;

                    GpBeat beat;
                    const juce::XmlElement* const rhythm = beat_element.getChildByName("Rhythm");
                    const auto rhythm_entry = rhythms_table.find(
                        rhythm == nullptr ? -1 : rhythm->getIntAttribute("ref", -1));
                    if (rhythm_entry == rhythms_table.end())
                    {
                        return invalidScore("beat references a missing rhythm");
                    }
                    auto duration = rhythmDuration(*rhythm_entry->second);
                    if (!duration.has_value())
                    {
                        return std::unexpected{std::move(duration.error())};
                    }
                    beat.duration_whole = *duration;
                    beat.grace = beat_element.getChildByName("GraceNotes") != nullptr;
                    beat.tremolo = beat_element.getChildByName("Tremolo") != nullptr;
                    beat.whammy = beat_element.getChildByName("Whammy") != nullptr ||
                                  beat_element.getChildByName("WhammyExtend") != nullptr;

                    for (const int note_id : idList(childText(beat_element, "Notes")))
                    {
                        const auto note_entry = notes_table.find(note_id);
                        if (note_entry == notes_table.end())
                        {
                            return invalidScore("beat references a missing note");
                        }
                        beat.notes.push_back(parseNote(*note_entry->second));
                    }

                    beats.push_back(std::move(beat));
                }
                track_bar.voices.push_back(std::move(beats));
            }

            score.tracks[track_index].bars.push_back(std::move(track_bar));
        }

        score.master_bars.push_back(std::move(bar));
    }

    return score;
}

} // namespace rock_hero::editor::core
