/*!
\file tab_view_state.h
\brief Seconds-resolved chart state rendered by the 2D tablature lane.
*/

#pragma once

#include <compare>
#include <optional>
#include <rock_hero/common/core/chart/chart.h>
#include <string>
#include <vector>

namespace rock_hero::common::core
{

/*! \brief One bend curve point resolved to an absolute timeline second. */
struct TabBendPointView
{
    /*! \brief Absolute timeline position of this curve point. */
    double seconds{0.0};

    /*! \brief Bend amount in semitones at this point. */
    double semitones{0.0};

    /*!
    \brief Compares two bend points by their stored fields.
    \param lhs Left-hand point.
    \param rhs Right-hand point.
    \return True when both points store equal values.
    */
    friend constexpr bool operator==(
        const TabBendPointView& lhs, const TabBendPointView& rhs) noexcept
    {
        // Hand-written, not defaulted: a defaulted comparison trips clang's -Wfloat-equal on a
        // floating member, which is why every float-bearing view here is spelled out. Exact
        // equality is intended; the ordering query expresses it warning-free with identical
        // semantics (NaN compares unequal either way).
        return std::is_eq(lhs.seconds <=> rhs.seconds) &&
               std::is_eq(lhs.semitones <=> rhs.semitones);
    }
};

/*! \brief One slide waypoint resolved to an absolute timeline second. */
struct TabSlideView
{
    /*! \brief Absolute timeline position the glide reaches its target fret. */
    double seconds{0.0};

    /*! \brief Target fret reached at this waypoint. */
    int fret{0};

    /*!
    \brief True when this waypoint is unpitched travel rather than a pitched arrival.

    NOT "the glide trails off here". A pick slide's every waypoint carries this, because a scrape's
    whole path is unpitched travel — the turnarounds included — so reading it as an ending mis-draws
    every scrape. An ordinary note's unpitched trail-off also sets it, and there the release reading
    does hold, but the field itself only ever says "this position is not a pitched stop".
    */
    bool unpitched{false};

    /*!
    \brief True when the glide continues the same note rather than ending it.

    Decided by the waypoint's place in the sustain and nothing else: strictly inside means the note
    is still sounding, so the linked continuation head draws in the note's own head shape; exactly
    at the sustain end means a shift-slide glide-end, where the note stops and the re-picked landing
    draws its own head, so no linked glyph.

    Being \ref unpitched does not unlink a waypoint — a scrape's turnaround is one gesture
    continuing, and its head is what keeps the corner from reading as a break.
    */
    bool linked{true};

    /*!
    \brief Compares two slide waypoints by their stored fields.
    \param lhs Left-hand waypoint.
    \param rhs Right-hand waypoint.
    \return True when both waypoints store equal values.
    */
    friend constexpr bool operator==(const TabSlideView& lhs, const TabSlideView& rhs) noexcept
    {
        return std::is_eq(lhs.seconds <=> rhs.seconds) && lhs.fret == rhs.fret &&
               lhs.unpitched == rhs.unpitched && lhs.linked == rhs.linked;
    }
};

/*! \brief One sounding note resolved to timeline seconds for rendering. */
struct TabNoteView
{
    /*! \brief Absolute onset position. */
    double start_seconds{0.0};

    /*! \brief Absolute end of the sustain; equals start_seconds when there is no sustain. */
    double end_seconds{0.0};

    /*! \brief One-based string, counted from the lowest-pitched string. */
    int string{1};

    /*! \brief Fret sounded; zero is the open string. */
    int fret{0};

    /*! \brief How the onset is produced. */
    NoteAttack attack{NoteAttack::Pick};

    /*! \brief Muting applied to the note. */
    NoteMute mute{NoteMute::None};

    /*!
    \brief Harmonic node in fret units, and the assertion that this note is a harmonic.

    Mirrors `ChartNote::harmonic_node`: presence is the whole test, and every harmonic carries
    one, a pinch included (rule-enforced). 2D has no fretboard axis to place a node on, so this
    positions nothing — it selects the diamond head and supplies the head's node label.
    */
    std::optional<double> harmonic_node{};

    /*! \brief True when the note is played with vibrato. */
    bool vibrato{false};

    /*! \brief True when the note is tremolo picked. */
    bool tremolo{false};

    /*! \brief True when the note is accented. */
    bool accent{false};

    /*! \brief Bend curve points in ascending time order; empty when not bent. */
    std::vector<TabBendPointView> bend;

    /*! \brief Slide waypoints in ascending time order; empty when the note does not slide. */
    std::vector<TabSlideView> slides;

    /*!
    \brief Compares two note views by their stored fields.
    \param lhs Left-hand note view.
    \param rhs Right-hand note view.
    \return True when both views store equal values.
    */
    friend bool operator==(const TabNoteView& lhs, const TabNoteView& rhs)
    {
        return std::is_eq(lhs.start_seconds <=> rhs.start_seconds) &&
               std::is_eq(lhs.end_seconds <=> rhs.end_seconds) && lhs.string == rhs.string &&
               lhs.fret == rhs.fret && lhs.attack == rhs.attack && lhs.mute == rhs.mute &&
               lhs.harmonic_node == rhs.harmonic_node && lhs.vibrato == rhs.vibrato &&
               lhs.tremolo == rhs.tremolo && lhs.accent == rhs.accent && lhs.bend == rhs.bend &&
               lhs.slides == rhs.slides;
    }
};

/*! \brief One chord-template posture note bracketed at an arpeggio start. */
struct TabArpeggioNoteView
{
    /*! \brief One-based string, counted from the lowest-pitched string. */
    int string{1};

    /*! \brief Fret held; zero is the open string. */
    int fret{0};

    /*!
    \brief True when a chart note actually sounds on this string exactly at the span start;
    false for posture strings that are held but struck later in the arpeggio.
    */
    bool sounded{false};

    /*!
    \brief Compares two arpeggio note views by their stored fields.
    \param lhs Left-hand arpeggio note view.
    \param rhs Right-hand arpeggio note view.
    \return True when both arpeggio note views store equal values.
    */
    friend constexpr bool operator==(
        const TabArpeggioNoteView& lhs, const TabArpeggioNoteView& rhs) noexcept = default;
};

/*! \brief One hand-posture span resolved to timeline seconds for rendering. */
struct TabShapeView
{
    /*! \brief Absolute start of the span. */
    double start_seconds{0.0};

    /*! \brief Absolute end of the span. */
    double end_seconds{0.0};

    /*! \brief Chord template display name; may be empty for unnamed shapes. */
    std::string name;

    /*!
    \brief True when the span's notes arrive sequentially (arpeggio bracket) rather than
    together (chord box). Derived at projection time from the notes under the span start.
    */
    bool arpeggio{false};

    /*!
    \brief Every template posture note, in ascending string order. Populated only for arpeggio
    spans, where each renders bracket marks at the span start — around the sounded note's full
    head, or around a bare fret number for strings struck later in the arpeggio.
    */
    std::vector<TabArpeggioNoteView> arpeggio_notes;

    /*!
    \brief Compares two shape views by their stored fields.
    \param lhs Left-hand shape view.
    \param rhs Right-hand shape view.
    \return True when both views store equal values.
    */
    friend bool operator==(const TabShapeView& lhs, const TabShapeView& rhs)
    {
        return std::is_eq(lhs.start_seconds <=> rhs.start_seconds) &&
               std::is_eq(lhs.end_seconds <=> rhs.end_seconds) && lhs.name == rhs.name &&
               lhs.arpeggio == rhs.arpeggio && lhs.arpeggio_notes == rhs.arpeggio_notes;
    }
};

/*! \brief One fret-hand position marker resolved to a timeline second. */
struct TabFhpView
{
    /*! \brief Absolute position the hand arrives at this placement. */
    double seconds{0.0};

    /*! \brief Lowest fret under the index finger. */
    int fret{1};

    /*! \brief Fret span covered by the hand. */
    int width{4};

    /*!
    \brief Compares two fret-hand position views by their stored fields.
    \param lhs Left-hand view.
    \param rhs Right-hand view.
    \return True when both views store equal values.
    */
    friend constexpr bool operator==(const TabFhpView& lhs, const TabFhpView& rhs) noexcept
    {
        return std::is_eq(lhs.seconds <=> rhs.seconds) && lhs.fret == rhs.fret &&
               lhs.width == rhs.width;
    }
};

/*!
\brief Seconds-resolved chart content for the current arrangement's tablature lane.

Built once per chart and shared immutably: positions are resolved through the tempo map at
projection time so rendering never queries musical positions per frame.
*/
struct TabViewState
{
    /*! \brief Number of string lanes the chart uses; zero means no chart is loaded. */
    int string_count{0};

    /*!
    \brief Capo fret from the chart tuning; 0 means no capo.

    Carried so the lane can indicate the string floor: the chart stores absolute frets with 0
    meaning the capo'd open string, so without this nothing in the drawn content says a capo
    exists at all.
    */
    int capo{0};

    /*! \brief Sounding notes in ascending onset order. */
    std::vector<TabNoteView> notes;

    /*! \brief Hand-posture spans in ascending start order. */
    std::vector<TabShapeView> shapes;

    /*! \brief Fret-hand position markers in ascending order. */
    std::vector<TabFhpView> fret_hand_positions;

    /*!
    \brief Compares two tab view states by their stored fields.
    \param lhs Left-hand state.
    \param rhs Right-hand state.
    \return True when both states store equal values.
    */
    friend bool operator==(const TabViewState& lhs, const TabViewState& rhs) = default;
};

} // namespace rock_hero::common::core
