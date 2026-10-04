/*!
\file timeline_ruler.h
\brief Pinned bars-and-beats ruler with tempo and signature header bands.
*/

#pragma once

#include "shared/flash_pulse.h"

#include <compare>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>
#include <optional>
#include <rock_hero/common/core/chart/chart.h>
#include <rock_hero/common/core/timeline/fraction.h>
#include <rock_hero/common/core/timeline/tempo_map.h>
#include <rock_hero/common/core/timeline/timeline.h>
#include <rock_hero/editor/core/timeline/tempo_grid_geometry.h>
#include <rock_hero/editor/core/timeline/time_signature_target.h>
#include <vector>

namespace rock_hero::editor::ui
{

/*!
\brief Fixed ruler component height in pixels.

The vertical layout this height accommodates — the section, tempo, and signature chip rows on
top, then the ruler body with the measure-number row and tick band — is private to
timeline_ruler.cpp; change this constant only together with those row constants.
*/
inline constexpr int g_timeline_ruler_height{68};

/*!
\brief One song-section name shown as a chip at the section's start.

Song-level view data, not ruler data: the controller's section projection owns the names and
start times, and the ruler renders them in its grid header as pinned, grid-anchored navigation
chips — exactly as the tempo and signature rows render tempo-map data.
*/
struct RulerSectionLabel
{
    /*! \brief Absolute timeline second of the section start the label anchors to. */
    double seconds{0.0};

    /*!
    \brief Musical position the section starts at.

    The section's identity in the song, carried so a chip click names a section by position
    instead of the ruler re-deriving musical time from the pixel it was clicked at.
    */
    common::core::GridPosition position{};

    /*! \brief Section display name; never empty (unnamed sections get no label). */
    juce::String name;

    /*! \brief True when this section is the formally selected one, drawn with an accent outline. */
    bool selected{false};

    /*!
    \brief Compares two section labels by their stored values.
    \param lhs Left-hand label.
    \param rhs Right-hand label.
    \return True when both labels store equal values.
    */
    friend bool operator==(const RulerSectionLabel& lhs, const RulerSectionLabel& rhs)
    {
        // Hand-written, not defaulted: a defaulted comparison trips clang's -Wfloat-equal on the
        // floating member. The ordering query expresses exact equality warning-free.
        return std::is_eq(lhs.seconds <=> rhs.seconds) && lhs.position == rhs.position &&
               lhs.name == rhs.name && lhs.selected == rhs.selected;
    }
};

/*!
\brief Draws the pinned bars-and-beats ruler above the scrollable timeline content.

The ruler stays fixed while the timeline content scrolls under it: the section, tempo, and
signature chip rows sit on top, and the ruler body below them holds the measure-number row and
the tick band. Each chip drops a dotted leader line in its own color down to the ruler body so
its exact position stays readable, with every chip drawn above every leader. Callers push the
current view geometry and the shared visible-span grid lines; the ruler renders everything from
that one scan result. Clicks convert into the same snapped cursor-placement intent as
timeline-content clicks.
*/
class TimelineRuler final : public juce::Component
{
public:
    /*!
    \brief Listener for the chip intents the ruler's grid header raises.

    An interface rather than more callback members because the chips and the menus raise many
    distinct intents, some of which need a prompt the ruler must not own; the tone strip's listener
    has the same shape for the same reason.
    */
    class Listener
    {
    public:
        /*! \brief Destroys the listener interface. */
        virtual ~Listener() = default;

        /*!
        \brief Called when the user clicks a section chip, or clicks away from every chip.
        \param position Position of the clicked section, or empty to clear the selection.
        */
        virtual void onSongSectionSelected(std::optional<common::core::GridPosition> position) = 0;

        /*!
        \brief Called when the user clicks a tempo chip.
        \param position Grid position the clicked chip's anchor pins.
        */
        virtual void onTempoAnchorSelected(common::core::GridPosition position) = 0;

        /*!
        \brief Called for each pointer event of a drag that began on a tempo chip, once it has
        passed JUCE's drag threshold.

        Grab-point relative, so the chip keeps its distance from the pointer instead of jumping to
        it, and measured in the content's time so a scroll during the drag does not move the anchor.
        The ruler only reports; whether the anchor moves is the listener's answer.

        \param position Grid position the dragged chip's anchor pins.
        \param milliseconds Time the anchor is asked to move to: its time at the press plus the
        time the pointer has travelled since, rounded once to whole milliseconds.
        */
        virtual void onTempoAnchorMoveRequested(
            common::core::GridPosition position, std::int64_t milliseconds) = 0;

        /*!
        \brief Called when the pointer releases after a press on a tempo chip.

        The drag's one boundary, reported whether the pointer moved or not: whether the release
        commits anything is the listener's answer.
        */
        virtual void onTempoAnchorDragReleased() = 0;

        /*!
        \brief Called when the user clicks a time-signature chip.
        \param measure Measure the clicked chip's signature change starts.
        */
        virtual void onTimeSignatureSelected(int measure) = 0;

        /*!
        \brief Called when the ruler menu asks to add a section at the clicked measure.

        The listener owns the prompt: a section needs a name, and the ruler must not raise one. A
        pointer menu inserts where the pointer is; the keyboard chord is the marker-rule form.

        \param position Grid position the menu was opened over; the verb snaps it to its
        measure's downbeat.
        */
        virtual void onSongSectionInsertPromptRequested(common::core::GridPosition position) = 0;

        /*!
        \brief Called when the tempo row's menu asks to add a tempo anchor at the clicked slot.
        \param position Grid position the menu was opened over, read at the placement quantum.
        */
        virtual void onTempoAnchorInsertRequested(common::core::GridPosition position) = 0;

        /*!
        \brief Called when the signature row's menu asks to add a time signature at the clicked
        measure.

        The listener owns the prompt: a time signature needs its meter typed, and the ruler must
        not raise one.

        \param target The insert the core's meter rule (\ref core::timeSignatureTargetAt) names
        at the measure the menu was opened over; the menu offers the row only where it names one.
        */
        virtual void onTimeSignatureInsertPromptRequested(
            const core::InsertTimeSignatureTarget& target) = 0;

        /*!
        \brief Called when the tempo row's menu asks to scale the whole song's tempo.

        The listener owns the prompt: the factor is typed, and the ruler must not raise one.
        */
        virtual void onScaleTempoPromptRequested() = 0;

        /*!
        \brief Called when a chip double-click, the section menu's Rename, the tempo menu's Set
        Tempo or the signature menu's Set Time Signature asks to restate the chip the ruler has
        just selected.

        The listener opens whatever the core publishes as the selection's restate verb, so the
        ruler decides nothing about what the prompt is.
        */
        virtual void onSelectionRestateRequested() = 0;

        /*! \brief Called when a ruler menu asks to delete the marker it has just selected. */
        virtual void onSelectionDeleteRequested() = 0;

        /*!
        \brief Called when the section menu asks to move the section it has just selected one
        measure.
        \param later True to move a measure later, false to move a measure earlier.
        */
        virtual void onSelectionMoveRequested(bool later) = 0;

    protected:
        /*! \brief Creates the listener interface. */
        Listener() = default;

        /*! \brief Copies the listener interface. */
        Listener(const Listener&) = default;

        /*! \brief Moves the listener interface. */
        Listener(Listener&&) = default;

        /*!
        \brief Assigns the listener interface from another interface.
        \return Reference to this listener interface.
        */
        Listener& operator=(const Listener&) = default;

        /*!
        \brief Move-assigns the listener interface from another interface.
        \return Reference to this listener interface.
        */
        Listener& operator=(Listener&&) = default;
    };

    /*! \brief Receives the timeline position of a cursor-placement click. */
    using CursorPlacementCallback = std::function<void(common::core::TimePosition position)>;

    /*! \brief Names the component for tests and enables direct cursor-placement clicks. */
    TimelineRuler();

    /*!
    \brief Stores whether the ruler should draw musical position data.
    \param project_loaded True while a project is loaded; false leaves the ruler as plain chrome.
    */
    void setProjectLoaded(bool project_loaded);

    /*!
    \brief Stores the ruler geometry derived from the viewport and zoomed content.

    Does not rebuild cached tick geometry by itself: tick coordinates come from the grid lines,
    so callers must follow every view change with a setGridLines push for the new span.

    \param timeline_range Full timeline range represented by the zoomed content width.
    \param content_width Width of the scrollable timeline canvas in pixels.
    \param view_x Horizontal scroll offset of the viewport into the zoomed timeline canvas.
    */
    void setTimelineView(common::core::TimeRange timeline_range, int content_width, int view_x);

    /*!
    \brief Positions the ruler's aligned play-from-here mark.

    The position is stored in seconds and mapped to a column at paint time, so the mark follows
    horizontal scrolling without needing a fresh push.

    \param cursor_position Where playback would start (the marker model): the moving playhead
    while playing, else the marker — the armed caret's slot or the passive transport rest. Absent
    only without a loaded project.
    \param paused True while the transport is paused, so the mark's line draws in the paused
    cursor color and reads as one continuous indicator with the content column below it; the
    flag triangle stays playback white in both states.
    */
    void setCursorPosition(std::optional<common::core::TimePosition> cursor_position, bool paused);

    /*!
    \brief Publishes whether the marker plane is open for selection and editing.

    The core's availability answer as published in \ref core::EditorViewState::marker_edits_enabled;
    the ruler derives nothing of its own from it and never reads the transport to decide it. While
    false a chip click falls through to cursor placement and seeks like any other ruler column, a
    chip double-click opens no prompt, and every row of the section, tempo and signature menus is
    disabled.

    \param marker_edits_enabled Published marker-plane availability.
    */
    void setMarkerEditsEnabled(bool marker_edits_enabled);

    /*!
    \brief Stores the tempo map that supplies anchors and click snapping, plus the note value ruler
    clicks quantize onto.

    Does not rebuild cached geometry by itself: callers must follow every grid change with a
    setGridLines push, matching setTimelineView. The drawn lines come from setGridLines, so the
    ruler never needs the grid note value itself — only the quantum its clicks land on.

    \param tempo_map Song tempo map shared with the track grid and snapping.
    \param placement_quantum Note value positions quantize to.
    */
    void setGrid(const common::core::TempoMap& tempo_map, common::core::Fraction placement_quantum);

    /*!
    \brief Stores the visible-span grid lines and rebuilds the cached ruler geometry from them.

    The lines are computed once by the owning view for the current visible span and shared with
    the track content, so the ruler never runs its own tempo-map scan.

    \param grid_lines Visible tempo-grid lines in content coordinates.
    */
    void setGridLines(std::vector<core::TempoGridLine> grid_lines);

    /*!
    \brief Stores the callback that receives cursor-placement seek positions.
    \param callback Callback invoked with the placement time of each ruler click.
    */
    void setCursorPlacementCallback(CursorPlacementCallback callback);

    /*!
    \brief Stores the listener that receives the chips' intents.
    \param listener Listener that must outlive this ruler.
    */
    void setListener(Listener& listener);

    /*!
    \brief Stores the song's section names drawn as the header's top chip row.

    The active section pins to the left edge as the song scrolls. An unchanged list returns
    early because every controller state push repeats it.

    \param labels Section names in ascending start order; empty clears the row.
    */
    void setSectionLabels(std::vector<RulerSectionLabel> labels);

    /*!
    \brief Stores which tempo and time-signature chips are selected.

    The two rows draw straight from the tempo map, so a selection names its chip by what the map
    stores: the anchor's grid position and the change's measure. An unchanged selection returns
    early because every controller state push repeats it.

    \param tempo_anchor Grid position of the selected tempo chip's anchor, or empty when none is
    selected.
    \param signature_measure Measure of the selected signature chip's change, or empty.
    */
    void setSelectedTempoMapChips(
        std::optional<common::core::GridPosition> tempo_anchor,
        std::optional<int> signature_measure);

    /*!
    \brief Flashes the tempo chip whose anchor move was refused: it glows red a couple of times,
    then the row stands as it was.

    A flash arriving while one runs joins it on the running pulse, so a drag or a held key reads as
    one pulse train. The chip is found by its anchor at paint time, so a chip out of view draws no
    glow.

    \param position Grid position of the refused anchor.
    */
    void flashTempoAnchorRefusal(common::core::GridPosition position);

    /*!
    \brief Flashes the measure numbers a time-signature edit renumbered: every number from a
    measure on lights in the accent once and fades back.

    The numbers are the ones the ruler already derives from the map, so the flash only picks which
    of them light, by measure, at paint time. A flash arriving while one runs replaces it.

    \param from_measure The first renumbered measure, the first whose number lights.
    */
    void flashRenumberedMeasures(int from_measure);

    /*!
    \brief Paints the measure-number row and the grid header's ticks and chip rows.
    \param g Graphics context used for drawing.
    */
    void paint(juce::Graphics& g) override;

    /*!
    \brief Converts ruler clicks into the same snapped placement intent as timeline-content
    clicks.
    \param event JUCE mouse event relative to this ruler.
    */
    void mouseDown(const juce::MouseEvent& event) override;

    /*!
    \brief Asks the listener to move the pressed tempo chip's anchor once the drag passes JUCE's
    drag threshold, so a click stays a click.
    \param event JUCE mouse event relative to this ruler.
    */
    void mouseDrag(const juce::MouseEvent& event) override;

    /*!
    \brief Ends a tempo-chip drag; the anchor stands wherever its last accepted move left it.
    \param event JUCE mouse event relative to this ruler.
    */
    void mouseUp(const juce::MouseEvent& event) override;

    /*!
    \brief Asks for the selection's restate on a double-clicked section, tempo or signature chip.
    \param event JUCE mouse event relative to this ruler.
    */
    void mouseDoubleClick(const juce::MouseEvent& event) override;

    /*! \brief Refreshes cached grid-line geometry after a resize changes the visible width. */
    void resized() override;

    /*!
    \brief The selected chip's bounds in this ruler's coordinates, when a row placed it where its
    marker is.

    A selected marker whose chip scrolled off the ruler has no bounds here — the rows place only
    the chips the window shows — and neither has one whose chip is PINNED at the edge for a marker
    standing elsewhere: the pinned name proves nothing about where the marker is.

    \return The chip's bounds, or empty.
    */
    [[nodiscard]] std::optional<juce::Rectangle<int>> selectedChipBounds() const;

    /*!
    \brief Where the tempo menu's "Add Tempo Anchor" inserts for a right-click at a point.

    The click read at the placement quantum the core published (\ref setGrid), the slot the tempo
    chord would read at a cursor standing there, so the pointer and the keyboard place alike.

    \param click Point in this ruler's coordinates.
    \return The insert's grid position, or empty when the geometry cannot map the click.
    */
    [[nodiscard]] std::optional<common::core::GridPosition> tempoInsertPositionAt(
        juce::Point<float> click) const;

    /*!
    \brief The signature row's menu for a right-click at a point, with the rows that apply there.

    "Add Time Signature" where the click's measure has no time signature on its downbeat and may
    start one; "Set Time Signature..." and "Delete Time Signature" over a chip. Every row is
    disabled while the marker plane is closed (\ref setMarkerEditsEnabled). Building the menu
    selects nothing; the right-click selects the chip before it shows the menu.

    \param click Point in this ruler's coordinates.
    \return The menu, as the right-click there would show it.
    */
    [[nodiscard]] juce::PopupMenu signatureMenuAt(juce::Point<float> click) const;

    /*!
    \brief The tempo row's menu for a right-click at a point, with the rows that apply there.

    "Add Tempo Anchor" where the click maps to a slot; "Set Tempo..." and "Delete Tempo Anchor"
    over a chip; "Scale Tempo..." everywhere, since it acts on the whole song. Every row is disabled
    while the marker plane is closed (\ref setMarkerEditsEnabled). Building the menu selects
    nothing; the right-click selects the chip before it shows the menu.

    \param click Point in this ruler's coordinates.
    \return The menu, as the right-click there would show it.
    */
    [[nodiscard]] juce::PopupMenu tempoMenuAt(juce::Point<float> click) const;

private:
    // A ruler text label already resolved to a non-overlapping draw position.
    struct RulerLabel
    {
        int x{0};
        juce::String text{};
        int width{0};
    };

    // One placed measure number, with the measure it numbers so the renumber flash can pick the
    // numbers it lights without re-reading the text.
    struct MeasureLabel
    {
        RulerLabel label{};
        int measure{0};
    };

    // One drawn chip: its placed label plus the index of the marker it stands for in the row's
    // source (the section list, the tempo map's anchors, its signature changes), so a click
    // resolves to that marker without re-deriving musical time, and the pinned chip (whose anchor
    // is off-screen) still names the marker it stands for. A tempo chip's text is its digits; the
    // quarter-note glyph ahead of them is drawn in its own font inside the same width.
    struct RulerChip
    {
        RulerLabel label{};
        std::size_t source_index{0};
        bool selected{false};
        // Standing at the left edge for a marker that is not there: the name is in view, the
        // marker is not, so this chip is no witness that the marker is on screen.
        bool pinned{false};
    };

    // What a press on a tempo chip arms for a drag, read off the published map at the press: the
    // anchor it moves, the time the anchor held then, and the content time under the pointer then.
    // Each drag event asks for the press time plus how far the pointer has travelled in content
    // time, so the anchor keeps its distance from the pointer and a scroll mid-drag moves nothing.
    struct TempoChipDrag
    {
        common::core::GridPosition position{};
        std::int64_t origin_milliseconds{0};
        double press_seconds{0.0};
    };

    // One chip row's cached geometry: the placed chips, and the visible marker columns its dotted
    // leaders drop from, which include markers whose chips were suppressed.
    struct ChipRow
    {
        std::vector<RulerChip> chips{};
        std::vector<int> leader_xs{};
    };

    // Maps an absolute timeline second to this pinned ruler's local x coordinate.
    [[nodiscard]] std::optional<float> localXForSeconds(double seconds) const noexcept;

    // Maps this pinned ruler's local x coordinate to the content time under it, at the current
    // scroll offset; empty when the geometry cannot map it.
    [[nodiscard]] std::optional<common::core::TimePosition> timeAtLocalX(
        float local_x) const noexcept;

    // Rebuilds the cached tick and label geometry from the stored grid lines, timeline geometry,
    // and tempo map. Kept out of paint() so cursor-only repaints, which happen at vblank cadence,
    // do not rebuild geometry or remeasure label text on every frame.
    void refreshRulerGeometry();

    // Rebuilds the three chip rows: a section chip per section start, a metronome marking
    // (enlarged quarter-note glyph plus chip-size digits) for the span each non-terminal anchor
    // starts, and a signature chip at each signature-change downbeat. The caller passes the
    // view-left time when pinning is active (empty otherwise) so every pinned row shares one gate.
    void refreshChipRows(std::optional<double> pinned_left_seconds);

    // Places one chip row over a row's markers, ascending by start: the pinned active marker at
    // the left edge, then a chip per visible start under row-wide overlap suppression. The selected
    // marker's chip is placed before any other, so density suppresses its neighbours instead of
    // it, and as the pin it never yields; a selection the keyboard walked onto is always drawn.
    // Text is formatted and measured only for a chip that could still be placed, since dense maps
    // offer far more candidates than survive. extra_width is room ahead of the text (the tempo
    // glyph).
    template <typename SecondsAt, typename TextAt>
    [[nodiscard]] ChipRow placeChipRow(
        std::size_t count, const SecondsAt& seconds_at, const TextAt& text_at, int extra_width,
        std::optional<double> pinned_left_seconds, std::optional<std::size_t> selected) const;

    // Draws visible grid ticks: body-height measures, short beats, and shorter subdivision ticks.
    void drawBeatTicks(juce::Graphics& g);

    // Draws one chip row's dotted leader lines from the row's bottom down to the ruler body,
    // marking each event's exact position — even where its chip was suppressed, and never for a
    // pinned chip, whose anchor is off-screen. Every chip row draws after every leader row, so a
    // chip always covers a leader passing through it.
    void drawChipLeaders(
        juce::Graphics& g, const std::vector<int>& anchor_xs, int row_y, juce::Colour color);

    // Draws the cached measure-number row, the numbers the renumber flash lights in its accent
    // blend and the rest in the plain ruler ink.
    void drawMeasureLabels(juce::Graphics& g);

    // Draws one chip's rounded fill, outlined when it stands for the selected marker, and leaves
    // the text color set; returns the chip's bounds for the text the row draws into it.
    static juce::Rectangle<float> drawChipFrame(
        juce::Graphics& g, const RulerChip& chip, int row_y, juce::Colour fill);

    // Draws one cached chip row with its text centered. The chips must have been measured with the
    // chip font.
    static void drawChipRow(
        juce::Graphics& g, const std::vector<RulerChip>& chips, juce::Colour fill, int row_y);

    // Draws the tempo chip row: each chip's glyph and digits in their own fonts, and the refusal
    // glow around the refused anchor's chip while its flash runs.
    void drawTempoChips(juce::Graphics& g);

    // Retires the tempo refusal flash and the renumber flash once their pulses have run, and
    // repaints what each lights while it runs.
    void stepFlashes();

    // Resolves a point to the chip under it in one row, or null when the point misses them all.
    [[nodiscard]] static const RulerChip* chipAt(
        const std::vector<RulerChip>& chips, int row_y, juce::Point<int> point);

    // One row of a ruler marker menu: its text and the verb choosing it runs.
    struct MarkerMenuRow
    {
        juce::String text;
        std::function<void()> action;
    };

    // The menu over rows, numbered by position, every row enabled only while the marker plane is
    // open.
    [[nodiscard]] juce::PopupMenu markerMenu(const std::vector<MarkerMenuRow>& rows) const;

    // The one policy every ruler menu shows by: the rows built (reading the chip) before
    // select_chip, when set, selects the chip the click landed on; then the menu opens and runs
    // the chosen row's action.
    void showMarkerMenu(std::vector<MarkerMenuRow> rows, const std::function<void()>& select_chip);

    // Opens the ruler's section menu: the add verb always, plus the section chip's rename, move
    // and delete when the click landed on one. The menu is the discoverable face of chords that
    // would otherwise only be reachable by keyboard.
    void showSectionContextMenu(const RulerChip* chip, juce::Point<float> click);

    // Opens the tempo row's menu (tempoMenuAt), selecting the chip the click landed on.
    void showTempoContextMenu(const RulerChip* chip, juce::Point<float> click);

    // The tempo menu's rows for a click: Add where the click maps to a slot, Set and Delete over a
    // chip, and Scale Tempo always.
    [[nodiscard]] std::vector<MarkerMenuRow> tempoRows(
        juce::Point<float> click, bool over_chip) const;

    // Opens the signature row's menu (signatureMenuAt), selecting the chip the click landed on.
    void showSignatureContextMenu(const RulerChip* chip, juce::Point<float> click);

    // The insert the signature menu's "Add Time Signature" opens for a right-click at a point: the
    // core's meter rule read at the measure the click is in, when that rule names an insert; empty
    // otherwise, or when the geometry cannot map the click.
    [[nodiscard]] std::optional<core::InsertTimeSignatureTarget> signatureInsertTargetAt(
        juce::Point<float> click) const;

    // The signature menu's rows for a click: Add when it offers an insert, Set and Delete over a
    // chip.
    [[nodiscard]] std::vector<MarkerMenuRow> signatureRows(
        juce::Point<float> click, bool over_chip) const;

    // The click's grid position read at a quantum: the placement quantum for the tempo insert, the
    // tick for the section insert, which floors it to its measure; empty when the geometry cannot
    // map the click.
    [[nodiscard]] std::optional<common::core::GridPosition> clickedGridPosition(
        juce::Point<float> click, common::core::Fraction quantum) const;

    // Draws the same transport cursor through the ruler for vertical alignment.
    void drawCursor(juce::Graphics& g);

    // Full timeline range represented by the zoomed content width.
    common::core::TimeRange m_timeline_range{};

    // Tempo map used for ruler measure ticks and anchor positions.
    common::core::TempoMap m_tempo_map{};

    // Note value ruler clicks quantize onto, initialized to the editor's default grid because the
    // Fraction default of 0/1 is a degenerate step.
    common::core::Fraction m_placement_quantum{core::g_default_tempo_grid_note_value};

    // Width of the scrollable timeline canvas that shares geometry with the grid.
    int m_content_width{0};

    // Horizontal scroll offset of the viewport into the zoomed timeline canvas.
    int m_view_x{0};

    // False while the editor is empty, so the ruler stays as plain chrome.
    bool m_project_loaded{false};

    // The aligned play-from-here mark's timeline position, stored in seconds and mapped to a column
    // at paint time so the mark follows the scroll offset. Absent while no mark is shown.
    std::optional<common::core::TimePosition> m_cursor_position{};

    // Whether the mark draws in the paused cursor color instead of the playback color.
    bool m_cursor_paused{false};

    // The core's published marker-plane availability: the one gate on the chip clicks, the
    // chip double-click and the section, tempo and signature menus' rows. Never derived here.
    bool m_marker_edits_enabled{false};

    // Callback invoked when the user clicks the ruler to place the transport cursor.
    CursorPlacementCallback m_cursor_placement_callback{};

    // Listener for the chips' intents; null until the owning view wires one, which keeps the ruler
    // usable as plain chrome in the layout tests that construct it alone.
    Listener* m_listener{nullptr};

    // The tempo-chip drag the current press armed, from the press on a chip to the release.
    std::optional<TempoChipDrag> m_tempo_chip_drag{};

    // Tempo-grid lines for the current visible span, pushed by the owning view so the ruler and
    // the track content share one tempo-map scan. Stored in content coordinates; ticks subtract
    // m_view_x, so the lines must be re-pushed after every view change.
    std::vector<core::TempoGridLine> m_grid_lines{};

    // Precomputed tick rectangles in local ruler coordinates, cached so paint() only issues one
    // fillRectList call instead of rebuilding geometry on every repaint.
    juce::RectangleList<float> m_tick_rects{};

    // Measure-number row of the ruler body: the pinned active measure at the left edge, then the
    // numbers that survived overlap suppression, with widths already measured. Kept out of paint()
    // because text-width measurement (GlyphArrangement layout) is comparatively expensive and would
    // otherwise run for every visible measure column on every repaint, including narrow cursor-only
    // repaints driven at vblank cadence or triggered by a single click.
    std::vector<MeasureLabel> m_measure_labels{};

    // Song-section names, pushed by the owning view whenever the song's sections change;
    // positions resolve via localXForSeconds when the chip row rebuilds.
    std::vector<RulerSectionLabel> m_section_source{};

    // The selected tempo chip's anchor beat and signature chip's measure, as last pushed.
    std::optional<common::core::GridPosition> m_selected_tempo_anchor{};
    std::optional<int> m_selected_signature_measure{};

    // The three chip rows, top down, cached for the same text-measurement reason as
    // m_measure_labels. Each row pins its active marker at the left edge, then places one chip per
    // visible marker start with row-wide overlap suppression.
    ChipRow m_section_row{};
    ChipRow m_tempo_row{};
    ChipRow m_signature_row{};

    // Width the quarter-note glyph takes ahead of every tempo chip's digits, measured with the
    // tempo rows so paint() never measures it.
    int m_tempo_glyph_width{0};

    // The tempo refusal flash while it runs: every refused anchor it has joined, named by position
    // because a chip row rebuild reassigns chips, and its pulse.
    std::optional<PulsedFlash<std::vector<common::core::GridPosition>>> m_tempo_refusal{};

    // The renumber flash while it runs: the first renumbered measure, the first whose number
    // lights, and its pulse.
    std::optional<PulsedFlash<int>> m_renumber_flash{};

    // Steps the flashes each display frame. Last, so every member the step reads is constructed
    // before the attachment can fire and destroyed after it.
    juce::VBlankAttachment m_frame_vblank{this, [this] { stepFlashes(); }};
};

} // namespace rock_hero::editor::ui
