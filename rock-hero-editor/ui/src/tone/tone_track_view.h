/*!
\file tone_track_view.h
\brief JUCE component that renders the tone track row and emits tone-region intents.
*/

#pragma once

#include "timeline/cursor_overlay.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>
#include <optional>
#include <rock_hero/common/audio/transport/i_transport.h>
#include <rock_hero/common/core/timeline/tempo_map.h>
#include <rock_hero/common/core/timeline/timeline.h>
#include <rock_hero/common/core/tone/tone_track.h>
#include <rock_hero/editor/core/tone/tone_track_view_state.h>
#include <string>

namespace rock_hero::editor::ui
{

/*!
\brief Renders tone regions from framework-free state and emits selection, resize, insert, and
delete intents.

Gestures follow the editor-wide interaction model (docs/plans/in-progress/editing-interaction-model.md):
a plain click selects, dragging an edge moves the shared boundary (snapped to the grid, Ctrl
bypassing to the fine grid, with a snap guide on the shared overlay), Alt is the insert quasimode
— click or press-drag-release inside a region requests a tone change at the snapped position,
with a ghost boundary line and copy cursor while Alt is held — and Esc cancels the gesture in
flight. The right-click menu mirrors every gesture (insert here, rename, delete).
*/
class ToneTrackView final : public juce::Component
{
public:
    /*! \brief Listener for user intents emitted by the tone track row. */
    class Listener
    {
    public:
        /*! \brief Destroys the listener interface. */
        virtual ~Listener() = default;

        /*!
        \brief Called when the user deliberately clicks an authored tone region.
        \param region_id Stable region id selected by the user.
        */
        virtual void onToneRegionSelected(std::string region_id) = 0;

        /*!
        \brief Called when the playhead crosses into a new region at render cadence.

        Makes that region's tone active (following the cursor) without a formal selection, so
        playback never leaves a deletable selection behind.
        */
        virtual void onToneRegionActivated() = 0;

        /*!
        \brief Called when an edge drag commits new snapped endpoints for a region.
        \param region_id Stable region id selected by the user.
        \param start New musical start (inclusive).
        \param end New musical end (exclusive).
        */
        virtual void onToneRegionResizeRequested(
            std::string region_id, common::core::GridPosition start,
            common::core::GridPosition end) = 0;

        /*!
        \brief Called when an edge drag commits a new position for the shared boundary between two
        adjacent regions, so both neighbors move together and coverage stays gap-free.
        \param right_region_id Region on the later side of the boundary.
        \param position New musical position for the shared boundary.
        */
        virtual void onToneBoundaryMoveRequested(
            std::string right_region_id, common::core::GridPosition position) = 0;

        /*!
        \brief Called when the user double-clicks a region to rename its tone.
        \param tone_document_ref Tone the region references.
        \param current_name Current tone name, used to pre-fill the rename prompt.
        */
        virtual void onToneRenamePromptRequested(
            std::string tone_document_ref, std::string current_name) = 0;

        /*!
        \brief Called when an Alt-click (or the region menu) requests a tone change at a position.

        The listener owns the payload policy: it opens the tone picker for the split, because a
        change to the same tone on both sides would be a no-op boundary the editor refuses.

        \param position Exact musical position for the new tone change, strictly inside a region.
        */
        virtual void onToneChangeInsertRequested(common::core::GridPosition position) = 0;

        /*!
        \brief Called when the region context menu requests deleting a region.

        Mirrors the Delete key on the selected region: the region's span merges into its
        neighbor (or the sole region resets).

        \param region_id Stable region id to delete.
        */
        virtual void onToneRegionDeleteRequested(std::string region_id) = 0;

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

    /*! \brief Receives the transient snap guide while an edge drag is active. */
    using SnapGuideCallback = std::function<void(std::optional<TimelineSnapGuide>)>;

    /*!
    \brief Creates the tone track row.
    \param listener Listener that receives tone-region intents.
    \param tempo_map Tempo map used to snap edge drags; referenced, not copied, so the owner must
    keep it alive for this view's lifetime.
    \param transport Read-only transport sampled at render cadence so the active region
    follows the playhead without controller round trips.
    */
    ToneTrackView(
        Listener& listener, const common::core::TempoMap& tempo_map,
        const common::audio::ITransport& transport);

    /*!
    \brief Stores the visible timeline range used to map region spans to pixels.
    \param visible_timeline Timeline range represented by the component width.
    */
    void setVisibleTimeline(common::core::TimeRange visible_timeline);

    /*!
    \brief Records the content x of the visible viewport's left edge so region labels pin there.

    The row scrolls inside the timeline viewport, so a region label drawn at the region's start
    scrolls off with it. Pinning the label to the visible left edge (like the tempo and time
    signature ruler) keeps the active tone name readable at all times.

    \param content_left_x Viewport view-position x, in this row's content coordinates.
    */
    void setVisibleContentLeft(int content_left_x);

    /*!
    \brief Applies the current tone-track render state.
    \param state State derived by the editor controller.
    */
    void setState(const core::ToneTrackViewState& state);

    /*!
    \brief Sets the grid step edge drags snap to, shared with the ruler and grid rendering.
    \param note_value Grid step as a fraction of a whole note.
    */
    void setGridNoteValue(common::core::Fraction note_value);

    /*!
    \brief Installs the callback that receives the transient edge-drag snap guide.
    \param on_snap_guide Callback receiving the guide, or empty when the drag ends.
    */
    void setSnapGuideCallback(SnapGuideCallback on_snap_guide);

    /*!
    \brief Reports whether the pointer at a local position targets an interactive region.

    Used by the cursor overlay's hit-test pass-through so region clicks reach this row while
    empty row space keeps the overlay's click-to-seek behavior.

    \param local_point Pointer position in this component's coordinates.
    \return True when the position hits an authored region body or edge.
    */
    [[nodiscard]] bool wantsPointerAt(juce::Point<int> local_point) const;

    /*!
    \brief Cancels the gesture in flight, restoring the pre-gesture state.

    The editor routes Esc here so an unwanted edge drag or Alt-insert placement can be abandoned
    without committing anything.

    \return True when a gesture was active and has been cancelled.
    */
    [[nodiscard]] bool cancelActiveGesture();

    /*!
    \brief Paints the row divider and the tone regions.
    \param g Graphics context used for drawing.
    */
    void paint(juce::Graphics& g) override;

    /*!
    \brief Updates the pointer cursor and the Alt-held insert ghost under the pointer.
    \param event Pointer event delivered by JUCE.
    */
    void mouseMove(const juce::MouseEvent& event) override;

    /*!
    \brief Begins an edge drag or Alt-insert placement, or arms a body click for selection.
    \param event Pointer event delivered by JUCE.
    */
    void mouseDown(const juce::MouseEvent& event) override;

    /*!
    \brief Previews snapped positions and reports the snap guide during a drag.
    \param event Pointer event delivered by JUCE.
    */
    void mouseDrag(const juce::MouseEvent& event) override;

    /*!
    \brief Commits the active gesture: a resize/insert intent, or a body click as a selection.
    \param event Pointer event delivered by JUCE.
    */
    void mouseUp(const juce::MouseEvent& event) override;

    /*!
    \brief Clears the Alt-held insert ghost when the pointer leaves the row.
    \param event Pointer event delivered by JUCE.
    */
    void mouseExit(const juce::MouseEvent& event) override;

    /*!
    \brief Opens the rename prompt for the tone of the region under the pointer.
    \param event Pointer event delivered by JUCE.
    */
    void mouseDoubleClick(const juce::MouseEvent& event) override;

private:
    // Which endpoint an active drag is moving.
    enum class EdgeKind : std::uint8_t
    {
        Start,
        End,
    };

    // Pointer target resolved by hit-testing the rendered regions.
    struct RegionHit
    {
        std::size_t region_index{};
        std::optional<EdgeKind> edge;
    };

    // Live edge-drag state; present only while a drag is active.
    struct DragState
    {
        std::size_t region_index{};
        EdgeKind edge{EdgeKind::Start};
        common::core::GridPosition preview_start;
        common::core::GridPosition preview_end;
    };

    // Live Alt-insert placement state: the boundary a release would create, previewed as a ghost
    // line and committed as one insert intent on mouse-up.
    struct InsertDragState
    {
        std::size_t region_index{};
        common::core::GridPosition preview;
    };

    // Maps one region's span to component x coordinates, or empty when unmappable.
    [[nodiscard]] std::optional<std::pair<float, float>> regionXSpan(
        const core::ToneRegionViewState& region) const;

    // Opens the right-click context menu for a region: insert-here (when the click position can
    // split the region), rename, and delete.
    void showRegionContextMenu(
        const core::ToneRegionViewState& region,
        std::optional<common::core::GridPosition> insert_position);

    // Resolves the authored region (and optionally its edge) under a local position.
    [[nodiscard]] std::optional<RegionHit> hitAt(juce::Point<int> local_point) const;

    // Snaps a drag x to the tempo grid for the active edge, accepted only inside the open interval
    // that keeps both regions sharing the dragged boundary non-empty; empty when out of range.
    [[nodiscard]] std::optional<common::core::GridPosition> snappedGridPositionForDrag(
        float x, const juce::ModifierKeys& mods) const;

    // Resolves an x to the snapped position a tone change would be inserted at, accepted only
    // strictly inside the given region (a change on an existing boundary splits nothing).
    [[nodiscard]] std::optional<common::core::GridPosition> insertPositionForX(
        float x, std::size_t region_index, const juce::ModifierKeys& mods) const;

    // Sets or clears the Alt-held ghost boundary line, repainting the strips it moves between.
    void setInsertGhostX(std::optional<float> ghost_x);

    // Maps a musical position to this row's x coordinate, when the geometry allows it.
    [[nodiscard]] std::optional<float> xForGridPosition(
        const common::core::GridPosition& position) const;

    // Reports the current snap guide, or clears it with an empty value.
    void emitSnapGuide(std::optional<TimelineSnapGuide> guide);

    // Listener that receives tone-region intents.
    Listener& m_listener;

    // Tempo map owned by the editor view state, referenced to snap edge drags to beats.
    const common::core::TempoMap& m_tempo_map;

    // Visible timeline range represented by the component width.
    common::core::TimeRange m_visible_timeline{};

    // Grid step edge drags snap to, shared with the ruler and grid rendering.
    common::core::Fraction m_grid_note_value{1, 4};

    // Content x of the visible viewport's left edge; region labels pin here as the row scrolls.
    int m_visible_content_left{0};

    // Last render state pushed by the editor controller.
    core::ToneTrackViewState m_state{};

    // Receives the transient snap guide while an edge drag is active.
    SnapGuideCallback m_on_snap_guide;

    // Recomputes the playhead region and emits one selection intent on boundary crossings.
    void advanceActiveRegion();

    // Read-only transport sampled at render cadence for cursor-follow highlighting.
    const common::audio::ITransport& m_transport;

    // Vblank-driven callback keeping the selection in step with playback crossings.
    juce::VBlankAttachment m_vblank_attachment;

    // Index of the region currently containing the playhead, if any.
    std::optional<std::size_t> m_active_region_index{};

    // Live edge-drag state; empty while no drag is active.
    std::optional<DragState> m_drag{};

    // Live Alt-insert placement state; empty while no insert gesture is active.
    std::optional<InsertDragState> m_insert_drag{};

    // The Alt-held hover ghost boundary x, or empty when no insert is possible under the pointer.
    std::optional<float> m_insert_ghost_x{};

    // Region index armed by a body press, committed as a selection on click release.
    std::optional<std::size_t> m_pending_select{};
};

} // namespace rock_hero::editor::ui
