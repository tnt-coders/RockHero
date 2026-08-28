/*!
\file gp_score.h
\brief Parsed Guitar Pro 7/8 score model consumed by the chart builder.

This is the minimal slice of the gpif document the importer needs: linear master bars with
signatures and section names, audio sync points, and per-track bars of timed beats carrying
notes with their technique fields. Repeats, directions, and notation-only detail are not modeled;
the parser rejects scores that would need them.
*/

#pragma once

#include <cstdint>
#include <optional>
#include <rock_hero/common/core/chart/chart.h>
#include <rock_hero/common/core/timeline/fraction.h>
#include <string>
#include <vector>

namespace rock_hero::editor::core
{

/*! \brief One audio sync point: a score position aligned to a backing-audio time. */
struct GpSyncPoint
{
    /*! \brief Zero-based master-bar index the sync point belongs to. */
    int bar{0};

    /*! \brief Position within the bar as a fraction of the bar, 0 at the downbeat. */
    double bar_fraction{0.0};

    /*! \brief Audio time of the score position in seconds. */
    double seconds{0.0};

    /*! \brief Playback tempo in quarter-note BPM from this point on. */
    double modified_tempo{0.0};
};

/*! \brief One master bar: the signature and section marker shared by every track. */
struct GpMasterBar
{
    /*! \brief Time signature numerator. */
    int numerator{4};

    /*! \brief Time signature denominator. */
    int denominator{4};

    /*! \brief Section name starting at this bar; empty when no section starts here. */
    std::string section;
};

/*! \brief Guitar Pro's seven-value bend model, offsets and values in percent. */
struct GpBend
{
    /*! \brief Bend amount at the onset, 100 = one whole step. */
    double origin_value{0.0};

    /*! \brief Bend amount held between the two middle offsets. */
    double middle_value{0.0};

    /*! \brief Bend amount at the note end. */
    double destination_value{0.0};

    /*! \brief Onset hold length in percent of the note duration. */
    double origin_offset{0.0};

    /*! \brief Middle plateau start in percent of the note duration. */
    double middle_offset1{50.0};

    /*! \brief Middle plateau end in percent of the note duration; equal offsets mean a point. */
    double middle_offset2{50.0};

    /*! \brief Destination hold start in percent of the note duration. */
    double destination_offset{100.0};
};

/*! \brief One note within a beat, with the technique fields the chart format can carry. */
struct GpNote
{
    /*! \brief Zero-based string, 0 = lowest-pitched string. */
    int string{0};

    /*! \brief Fret sounded; zero is the open string. */
    int fret{0};

    /*! \brief True when the next note on this string continues this one. */
    bool tie_origin{false};

    /*! \brief True when this note continues the previous note on this string. */
    bool tie_destination{false};

    /*! \brief True when this note is the destination of a hammer-on or pull-off. */
    bool hopo_destination{false};

    /*! \brief True for two-hand (right-hand) tapped onsets. */
    bool tapped{false};

    /*! \brief True for left-hand tapped onsets; imports as a hammer-on, not a tap. */
    bool left_hand_tapped{false};

    /*! \brief True for palm-muted notes. */
    bool palm_mute{false};

    /*! \brief True for fully muted (dead) notes. */
    bool full_mute{false};

    /*!
    \brief How wide the note's vibrato is, already on the chart's own axis.

    Guitar Pro spells the two tiers as the `Vibrato` element's text — `Slight` and `Wide` — which
    resolve onto the chart's `Narrow` and `Wide` one for one, so the score's house word for the
    ordinary tier is translated in the parser rather than carried through the model. An absent
    element is \ref common::core::VibratoState::Off; a PRESENT one is always a shake, so any
    spelling but `Wide` reads as the ordinary tier rather than dropping the mark. The corpus
    writes only the two words (318 `Slight`, 10 `Wide`).
    */
    common::core::VibratoState vibrato{common::core::VibratoState::Off};

    /*!
    \brief How hard the note is struck, already resolved onto the chart's one dynamics axis.

    The score spells this as two INDEPENDENT elements — an `Accent` bitset and a sibling
    `AntiAccent` — which could in principle both be set, so something has to reconcile them.
    That happens in the parser, beside the rest of the reading this model already interprets
    rather than mirrors: the same field folds the bitset's two loud tiers together and leaves its
    staccato bit to \ref GpNote::staccato, which is duration rather than dynamics. Resolving here
    rather than downstream is also what keeps the beat splitter honest, since clearing "the
    dynamics marks" from a repeated stroke is then one assignment that a third source flag could
    never fall out of.
    */
    common::core::NoteEmphasis emphasis{common::core::NoteEmphasis::Normal};

    /*!
    \brief True when the note is marked staccato, which is DURATION information, not dynamics.

    Guitar Pro spells staccato as bit 1 of the same `Accent` bitset the loud tiers ride in, but
    the mark says nothing about how hard the note is struck: playback sounds a staccato note for
    exactly half its stated duration. The builder therefore halves the imported ring per note and
    stores nothing — the short ring IS the record of the mark.
    */
    bool staccato{false};

    /*! \brief Guitar Pro slide flag bitset; zero when the note does not slide. */
    int slide_flags{0};

    /*! \brief Harmonic type name from the score; empty when not a harmonic. */
    std::string harmonic_type;

    /*! \brief Precise harmonic touch fret; absent when not a harmonic. */
    std::optional<double> harmonic_fret{};

    /*! \brief Bend curve; absent when the note is not bent. */
    std::optional<GpBend> bend{};

    /*!
    \brief The trill's auxiliary note as its ABSOLUTE PITCH value; absent when not trilled.

    Guitar Pro names the note the trill alternates with by pitch rather than by fret, so the fret
    it means on this string is the value minus what that string sounds stopped at the capo. The
    format states no SPEED at all — GP5's binary carried a period, gpif carries only this one
    number — so how fast the alternation runs is not a fact the score can supply.
    */
    std::optional<int> trill_value{};
};

/*! \brief Placement of a grace-note beat relative to the principal beat that follows it. */
enum class GpGracePlacement : std::uint8_t
{
    /*! \brief Not a grace beat. */
    None,

    /*! \brief Grace sounds ahead of the principal beat's position. */
    BeforeBeat,

    /*! \brief Grace sounds on the principal beat's position, delaying the principal. */
    OnBeat
};

/*!
\brief Which member of a ROLLED chord sounds first; None when the beat is not rolled.

Guitar Pro spells the mark `Arpeggio` with the text `Up` or `Down`, but neither word says which
string speaks first, and the answer is the opposite of the naive reading in one of the two cases.
The values are therefore named for what they MEAN rather than for the file's word, so the
treacherous mapping is stated exactly once — in the parser, where the word is read — and nothing
downstream can re-derive it differently.

The rolled chord is engraving's vertical wavy line: one grip sounded member by member. It is not
this project's ARPEGGIO span, which is a derived classification of what a chart already states.
*/
enum class GpRollDirection : std::uint8_t
{
    /*! \brief Not a rolled beat. */
    None,

    /*! \brief The lowest-pitched member sounds first (Guitar Pro's `Down`, a downstroke). */
    LowestFirst,

    /*! \brief The highest-pitched member sounds first (Guitar Pro's `Up`, an upstroke). */
    HighestFirst
};

/*! \brief One beat (a rhythm slot) within a voice: simultaneous notes or a rest. */
struct GpBeat
{
    /*! \brief Duration as a fraction of a whole note, after dots and tuplets. */
    common::core::Fraction duration_whole{1, 4};

    /*! \brief Grace placement of the beat; grace beats take no time from the bar. */
    GpGracePlacement grace{GpGracePlacement::None};

    /*!
    \brief Tremolo-picking stroke duration as a fraction of a whole note; a zero numerator
    means the beat is not tremolo picked.

    Guitar Pro's three tremolo speeds arrive as quarter-note fractions (1/2, 1/4, 1/8 for
    eighth, sixteenth, and thirty-second strokes); the parser converts to whole-note units so
    the builder can spell the strokes out without consulting the meter.
    */
    common::core::Fraction tremolo_stroke{};

    /*! \brief Which member a rolled chord sounds first; None when the beat is not rolled. */
    GpRollDirection roll_direction{GpRollDirection::None};

    /*!
    \brief How long a rolled chord takes to cross its members, in Guitar Pro's own MIDI ticks.

    Kept in the file's unit — 480 ticks to the quarter note, so 1920 to the whole — rather than
    converted at the parse boundary the way \ref GpBeat::tremolo_stroke is, because the stagger
    between members is this number divided by the gaps between them, and doing that division in
    ticks is what keeps every onset it produces on the chart's own grid. Zero when the score
    states no spread, which is a roll with no stagger to import.
    */
    int roll_spread_ticks{0};

    /*!
    \brief Where a rolled chord sits against its beat: 0 falls on the beat, 1 starts on it.

    Guitar Pro's second roll slider. At 1 the first member is struck on the beat and the rest
    follow; at 0 the roll ANTICIPATES, so its LAST member lands on the beat and the figure begins
    a whole spread earlier. The chart carries only the on-the-beat reading, so the builder counts
    every roll whose stated anticipation it places on the beat instead. Continuous in the tool,
    hence a double rather than a flag.
    */
    double roll_start_time{0.0};

    /*! \brief True when the beat carries a whammy-bar dive (not yet imported). */
    bool whammy{false};

    /*! \brief Notes sounding on this beat; empty for rests. */
    std::vector<GpNote> notes;
};

/*! \brief One track bar: the playable voices in play order. */
struct GpBar
{
    /*! \brief Valid voices of the bar, each a beat sequence; empty for an empty bar. */
    std::vector<std::vector<GpBeat>> voices;
};

/*! \brief One instrument track. */
struct GpTrack
{
    /*! \brief Track display name. */
    std::string name;

    /*! \brief Open-string MIDI pitches, index 0 = lowest-pitched string. */
    std::vector<int> tuning_midi;

    /*! \brief Capo fret; zero when uncapoed. */
    int capo{0};

    /*! \brief Bars in master-bar order; always master-bar count entries. */
    std::vector<GpBar> bars;
};

/*! \brief One parsed Guitar Pro score. */
struct GpScore
{
    /*! \brief Song title from the score metadata. */
    std::string title;

    /*! \brief Artist from the score metadata. */
    std::string artist;

    /*! \brief Album from the score metadata. */
    std::string album;

    /*! \brief Base tempo in quarter-note BPM, used only when sync points are absent. */
    double base_tempo_quarter_bpm{120.0};

    /*! \brief Master bars in score order. */
    std::vector<GpMasterBar> master_bars;

    /*! \brief Audio sync points in score order; empty when the score has no backing audio. */
    std::vector<GpSyncPoint> sync_points;

    /*!
    \brief Backing-track frame padding in fixed 44.1kHz frames.

    A positive value is silence Guitar Pro places before the audio to align a recording whose
    content starts after the score's first beat; the importer turns it into the backing audio's
    start offset. Always counted in 44100-rate frames regardless of the asset's real sample rate.
    */
    int frame_padding{0};

    /*! \brief Instrument tracks. */
    std::vector<GpTrack> tracks;

    /*! \brief Archive entry path of the backing audio; empty when none is embedded. */
    std::string embedded_audio_entry;
};

} // namespace rock_hero::editor::core
