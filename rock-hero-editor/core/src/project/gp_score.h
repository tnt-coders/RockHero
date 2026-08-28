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

    /*! \brief True for notes played with vibrato. */
    bool vibrato{false};

    /*!
    \brief How hard the note is struck, already resolved onto the chart's one dynamics axis.

    The score spells this as two INDEPENDENT elements — an `Accent` bitset and a sibling
    `AntiAccent` — which could in principle both be set, so something has to reconcile them.
    That happens in the parser, beside the rest of the reading this model already interprets
    rather than mirrors: the same field drops Guitar Pro's staccato bit and folds its two loud
    tiers together. Resolving here rather than downstream is also what keeps the beat splitter
    honest, since clearing "the dynamics marks" from a repeated stroke is then one assignment
    that a third source flag could never fall out of.
    */
    common::core::NoteEmphasis emphasis{common::core::NoteEmphasis::Normal};

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
