/*!
\file tab_paint_core.h
\brief Shared JUCE notation paint core rendering the 2D tablature for both products.

The one designated juce_graphics-bearing public header in rock-hero-common/ui (the 30-Q1
amendment in docs/design/architectural-principles.md "UI Modules"): the editor tab lane and the
game tab strips must produce identical notation pixels, so the rasterizer itself is shared and
each host supplies only bounds, timeline mapping, and state.
*/

#pragma once

#include <cstddef>
#include <functional>
#include <juce_graphics/juce_graphics.h>
#include <rock_hero/common/core/chart/chart_view_state.h>
#include <rock_hero/common/core/timeline/timeline.h>
#include <rock_hero/common/ui/tab/tab_lane_layout.h>
#include <vector>

namespace rock_hero::common::ui
{

/*!
\brief Returns the base display color for one string lane as a JUCE color.

The six highest lanes take the Charter Classic preset's standard colors anchored at the
sixth-highest lane; lower lanes continue with the extended tier (see stringLaneColor). This is
the paint core's JUCE conversion of the shared palette authority.

\param displayed_string Lane's string position, 1 = lowest displayed lane.
\param displayed_string_count Total number of displayed lanes.
\return Base lane color the tablature style derives its surfaces from.
*/
[[nodiscard]] juce::Colour tabStringColor(int displayed_string, int displayed_string_count);

/*!
\brief Returns the hand-shape mark color shared by the tab lane and name chips above it.

The Charter hand-shape base brightened so the narrow span rails and the chord/arpeggio name
chips read clearly against dark chrome; hosts drawing name chips derived from the same tab
projection must agree on it so a chip visually belongs to the rails below it.

\param arpeggio True for arpeggio spans (purple); false for chord spans (blue).
\return Opaque mark color.
*/
[[nodiscard]] juce::Colour tabShapeMarkColor(bool arpeggio);

/*!
\brief Returns the number a head carries for this note stopped at `fret_at_head`.

Any harmonic whose node lies on the neck names its **node** rather than its fret, because the node
is what sets the pitch. That is \ref rock_hero::common::core::nodeIsOnNeck, so it covers a tap
harmonic and an artificial or harp harmonic over a real stop as well as a natural one — the stop is
a genuine fret in those cases, and the label still names the node, because the node is what sounds.
One decimal is exactly enough: it separates every distinct node through the 17th harmonic, far
past the ~8th a fingertip can still isolate. A trailing ".0" is dropped so the common 12 / 7 / 5
positions stay as narrow as an ordinary fret number.

A **pinch** keeps its fret: its node sits off the neck over the pickups, and 2D has no axis to place
that on, so drawing it would name a fret the hand is nowhere near (roadmap 25-Q5).

The stop is a parameter because one gesture has more than one head: the onset passes the note's own
fret, and a linked slide junction passes the fret the glide has reached, so every head of a gesture
states the same QUANTITY (a harmonic labels nodes at all of them, not a node at the onset and a raw
fret at the junctions).

\param note Projected note to label.
\param fret_at_head Fret the head being labeled sits at — `note.fret` at the onset, the keyframe's
       fret at a linked junction.
\return Head text, never empty.
*/
[[nodiscard]] juce::String tabNoteHeadText(
    const common::core::NoteViewState& note, int fret_at_head);

/*!
\brief Returns the vertical center of one string lane inside JUCE component bounds.
\param displayed_string Lane's string position, 1 = lowest displayed lane.
\param displayed_string_count Total number of displayed lanes.
\param bounds Full tablature lane bounds.
\return Vertical lane center in the bounds' coordinate space.
*/
[[nodiscard]] inline float tabLaneCenterY(
    int displayed_string, int displayed_string_count, juce::Rectangle<int> bounds) noexcept
{
    return tabLaneCenterY(
        displayed_string,
        displayed_string_count,
        static_cast<float>(bounds.getY()),
        static_cast<float>(bounds.getHeight()));
}

/*!
\brief Lane geometry plus the JUCE-only facts one paint call needs.

Extends the framework-free TabLaneGeometry (which layout-manifest consumers share) with the
component bounds and the derived fonts, so every drawer of one paint call reads one authority.
*/
struct TabLaneMetrics : TabLaneGeometry
{
    /*! \brief Full tablature lane bounds in the graphics context's space. */
    juce::Rectangle<int> bounds;

    // Initialized via FontOptions because JUCE 8 deprecates the default Font constructor; the
    // placeholder values are replaced by makeTabLaneMetrics before any drawing.

    /*! \brief Bold fret-number font derived from the note height. */
    juce::Font fret_font{juce::FontOptions{}};

    /*! \brief Bend amount chip font derived from the note height. */
    juce::Font bend_font{juce::FontOptions{}};

    /*! \brief Bold label font for hand-shape and fret-hand-position chips. */
    juce::Font label_font{juce::FontOptions{}};

    /*!
    \brief Base color for a chart string, accounting for extra user lanes below the chart.
    \param chart_string One-based chart string, 1 = the chart's lowest string.
    \return Base lane color for the string.
    */
    [[nodiscard]] juce::Colour baseColor(int chart_string) const;
};

/*!
\brief Derives the metrics for one tablature paint call.

\param bounds Full tablature lane bounds; must not be empty.
\param visible_timeline Timeline range represented by the width; must have positive duration.
\param displayed_count Number of displayed lanes; must be positive.
\param chart_string_count String count declared by the displayed chart.
\param style Optional style variants; the default reproduces the editor lane exactly.
\return Metrics every drawer of the paint call reads.
*/
[[nodiscard]] TabLaneMetrics makeTabLaneMetrics(
    juce::Rectangle<int> bounds, common::core::TimeRange visible_timeline, int displayed_count,
    int chart_string_count, TabLaneStyle style = {});

/*!
\brief Strokes the outline of the silhouette this note's head is drawn with.

For host chrome that must trace a head it did not draw — the editor's selection ring is the one
caller today. The silhouette is chosen by the same rule the head itself uses, so a ring cannot
disagree with the head under it: re-deriving "diamond if it has a node, else a circle" in the host
left every pick slide wearing a circular ring around a plectrum once the scrape head shipped.

\param g Graphics context to draw into.
\param note Note whose head silhouette is wanted.
\param center_x Head center on the time axis.
\param center_y Head center on the lane's string line.
\param extent Head size, as \ref TabNoteLayout::head_size reports it.
\param stroke_thickness Outline thickness in pixels.
*/
void strokeTabNoteHeadOutline(
    juce::Graphics& g, const common::core::NoteViewState& note, float center_x, float center_y,
    float extent, float stroke_thickness);

/*!
\brief Draws the editor's pending fret entry box: the mute number-plate's own geometry and font
carrying a provisional value.

Host chrome, not notation — the game renders no keyboard entry — but exported from the core
rather than restated in the host so the provisional digit's typography and placement CANNOT
drift from the committed head's (the one-primitive rule of the pending-entry design; the insert
ghost's shape drifted exactly this way once). The plate rect is the same authority the mute
number-plate draws and the font is the head digit's own. The GROUND flips with validity, and
both grounds are known and internal: a valid value sits on the lane's near-black exactly like a
committed plated digit, and an invalid one flips to a white plate — red-on-white is the error
idiom at full contrast, and the plate polarity flip itself carries the signal in full
monochrome, the glance mechanism the mute plate-flip design established. The border and inks
are the host's, because pending is an editor state and this core owns no editor colors.

The box follows the head's own digit PLACEMENT too: a plectrum raises its number to fit the
silhouette, so the box over a scrape rides the same raise — the provisional digit must sit
exactly where the committed one will land.

\param g Graphics context to draw into.
\param metrics Metrics of the lane being painted.
\param note Note whose head the box rides, or null at an empty insert slot; supplies the head
       shape the digit placement follows.
\param center_x Box center on the time axis — a head's onset x, or an empty insert slot's x.
\param center_y The head's center on the lane's string line; the box derives the digit's own
       center from it.
\param text Provisional value exactly as typed.
\param light_plate True flips the box to the white invalid ground; false is the dark valid one.
\param text_color Text ink: the host's digit white while the value would apply, red when not.
\param border_color Box border: the host's editor accent.
*/
void paintTabPendingEntryBox(
    juce::Graphics& g, const TabLaneMetrics& metrics, const common::core::NoteViewState* note,
    float center_x, float center_y, const juce::String& text, bool light_plate,
    juce::Colour text_color, juce::Colour border_color);

/*!
\brief Answers which note the lane draws at one index, for a host composing two projected forms.

The editor's actual-ring reveal is the one caller: it draws each note in the chart's presented or
its \ref common::core::ChartNoteForm::Actual form, decided per note, and the two forms of one chart
align by index because presentation trims tails and never adds or removes a note. Handing the
choice in as an accessor keeps the composition rule wholly in the host — this core is given the
note to draw and never the reason.

An empty accessor is the ordinary case: every note draws from the state's own
\ref common::core::ChartViewState::notes.

It is asked once per VISIBLE note in each of the two note passes — the range cull runs first, on
the state's own onsets — so the indirection costs a call per note drawn, never one per glyph.
*/
using TabDrawnNote = std::function<const common::core::NoteViewState&(std::size_t index)>;

/*!
\brief Draws one tablature lane's visible chart content in Charter's layer order.

String lines, hand-shape spans, sustain tails with their slide and bend lines, arpeggio posture
brackets, note heads with technique glyphs, then the floating labels (slide frets and bend
amount chips) on top. Visibility is bounded by the graphics context's clip region widened by
head slack, so hosts repaint partial regions (tile strips, dirty rectangles) correctly.

\param g Graphics context to draw into; its clip bounds gate the visible span.
\param metrics Metrics from makeTabLaneMetrics for the lane being painted.
\param tab Seconds-resolved tab projection; string_count must be positive. Its notes order and
       count the lane's notes, and supply every one of them unless `drawn_note` picks another
       form's. Every tail is drawn to the DRAWN note's own end (NoteViewState::end_seconds); the
       span-implied hold (ChartViewState::display_hold_ends) is the 3D board's and is not read
       here.
\param prefix_max_end_seconds Running maximum of note ends (common::core::makeSustainPrefixMax)
       bounding the visible range. It must reach at least as far as every end DRAWN or a tail on
       screen is culled away, so a host composing two forms passes the table of the form whose
       tails run longest — conservative for both, since the passes below drop each note that
       really ends before the span.
\param drawn_note Per-index choice of which form's note to draw; empty draws `tab.notes`
       throughout.
*/
void paintTabLane(
    juce::Graphics& g, const TabLaneMetrics& metrics, const common::core::ChartViewState& tab,
    const std::vector<double>& prefix_max_end_seconds, const TabDrawnNote& drawn_note = {});

} // namespace rock_hero::common::ui
