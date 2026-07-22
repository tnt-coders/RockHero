/*!
\file tab_view_state.h
\brief Seconds-resolved chart state rendered by the 2D tablature lane.
*/

#pragma once

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
        const TabBendPointView& lhs, const TabBendPointView& rhs) noexcept = default;
};

/*! \brief One slide waypoint resolved to an absolute timeline second. */
struct TabSlideView
{
    /*! \brief Absolute timeline position the glide reaches its target fret. */
    double seconds{0.0};

    /*! \brief Target fret reached at this waypoint. */
    int fret{0};

    /*! \brief True when the glide trails off unpitched. */
    bool unpitched{false};

    /*!
    \brief True when the glide lands as an unpicked continuation of the same note.

    A legato landing renders the linked continuation head at the waypoint. False when a
    re-picked note sounds on the same string exactly at the waypoint (a shift slide's target):
    that note's own head renders, and drawing the linked head too would over-paint a picked
    note into looking unpicked.
    */
    bool linked{true};

    /*!
    \brief Compares two slide waypoints by their stored fields.
    \param lhs Left-hand waypoint.
    \param rhs Right-hand waypoint.
    \return True when both waypoints store equal values.
    */
    friend constexpr bool operator==(const TabSlideView& lhs, const TabSlideView& rhs) noexcept =
        default;
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

    /*! \brief Harmonic timbre applied to the note. */
    NoteHarmonic harmonic{NoteHarmonic::None};

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
    friend bool operator==(const TabNoteView& lhs, const TabNoteView& rhs) = default;
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
    friend bool operator==(const TabShapeView& lhs, const TabShapeView& rhs) = default;
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
    friend constexpr bool operator==(const TabFhpView& lhs, const TabFhpView& rhs) noexcept =
        default;
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
