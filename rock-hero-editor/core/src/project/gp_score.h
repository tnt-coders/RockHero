/*!
\file gp_score.h
\brief Parsed Guitar Pro 7/8 score model consumed by the chart builder.

This is the minimal slice of the gpif document the importer needs: linear master bars with
signatures and section names, audio sync points, and per-track bars of timed beats carrying
notes with their technique fields. Repeats, directions, and notation-only detail are not modeled;
the parser rejects scores that would need them.
*/

#pragma once

#include <optional>
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

    /*! \brief True for two-hand or left-hand tapped onsets. */
    bool tapped{false};

    /*! \brief True for palm-muted notes. */
    bool palm_mute{false};

    /*! \brief True for fully muted (dead) notes. */
    bool full_mute{false};

    /*! \brief True for notes played with vibrato. */
    bool vibrato{false};

    /*! \brief True for accented notes. */
    bool accent{false};

    /*! \brief Guitar Pro slide flag bitset; zero when the note does not slide. */
    int slide_flags{0};

    /*! \brief Harmonic type name from the score; empty when not a harmonic. */
    std::string harmonic_type;

    /*! \brief Precise harmonic touch fret; absent when not a harmonic. */
    std::optional<double> harmonic_fret{};

    /*! \brief Bend curve; absent when the note is not bent. */
    std::optional<GpBend> bend{};
};

/*! \brief One beat (a rhythm slot) within a voice: simultaneous notes or a rest. */
struct GpBeat
{
    /*! \brief Duration as a fraction of a whole note, after dots and tuplets. */
    common::core::Fraction duration_whole{1, 4};

    /*! \brief True for grace-note beats, which take no time from the bar. */
    bool grace{false};

    /*! \brief True when the beat is tremolo picked. */
    bool tremolo{false};

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

    /*! \brief Instrument tracks. */
    std::vector<GpTrack> tracks;

    /*! \brief Archive entry path of the backing audio; empty when none is embedded. */
    std::string embedded_audio_entry;
};

} // namespace rock_hero::editor::core
