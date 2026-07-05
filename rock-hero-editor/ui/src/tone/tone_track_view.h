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
\brief Renders tone regions from framework-free state and emits selection and resize intents.

Authored regions can be selected by clicking their body and resized by dragging their edges; edge
drags snap to whole tempo-map beats, clamp against neighboring regions and the terminal anchor,
and report a transient snap guide so the shared overlay can draw a full-height alignment line.
The synthesized legacy-default region is read-only and ignores the pointer entirely.
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
        \brief Called when the user clicks an authored tone region.
        \param region_id Stable region id selected by the user.
        */
        virtual void onToneRegionSelected(std::string region_id) = 0;

        /*!
        \brief Called when an edge drag commits new snapped endpoints for a region.
        \param region_id Stable region id selected by the user.
        \param start New musical start (inclusive).
        \param end New musical end (exclusive).
        */
        virtual void onToneRegionResizeRequested(
            std::string region_id, common::core::ToneGridPosition start,
            common::core::ToneGridPosition end) = 0;

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
    \brief Applies the current tone-track render state.
    \param state State derived by the editor controller.
    */
    void setState(const core::ToneTrackViewState& state);

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
    \brief Paints the row divider and the tone regions.
    \param g Graphics context used for drawing.
    */
    void paint(juce::Graphics& g) override;

    /*!
    \brief Updates the pointer cursor for region bodies and resizable edges.
    \param event Pointer event delivered by JUCE.
    */
    void mouseMove(const juce::MouseEvent& event) override;

    /*!
    \brief Begins an edge drag or arms a body click for selection.
    \param event Pointer event delivered by JUCE.
    */
    void mouseDown(const juce::MouseEvent& event) override;

    /*!
    \brief Previews snapped endpoints and reports the snap guide during an edge drag.
    \param event Pointer event delivered by JUCE.
    */
    void mouseDrag(const juce::MouseEvent& event) override;

    /*!
    \brief Commits an edge drag as a resize intent or a body click as a selection intent.
    \param event Pointer event delivered by JUCE.
    */
    void mouseUp(const juce::MouseEvent& event) override;

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
        common::core::ToneGridPosition preview_start;
        common::core::ToneGridPosition preview_end;
    };

    // Maps one region's span to component x coordinates, or empty when unmappable.
    [[nodiscard]] std::optional<std::pair<float, float>> regionXSpan(
        const core::ToneRegionViewState& region) const;

    // Resolves the authored region (and optionally its edge) under a local position.
    [[nodiscard]] std::optional<RegionHit> hitAt(juce::Point<int> local_point) const;

    // Converts a drag x coordinate into the snapped, clamped global beat for the active edge.
    [[nodiscard]] std::optional<std::int64_t> snappedBeatForDrag(float x) const;

    // Reports the current snap guide, or clears it with an empty value.
    void emitSnapGuide(std::optional<TimelineSnapGuide> guide);

    // Listener that receives tone-region intents.
    Listener& m_listener;

    // Tempo map owned by the editor view state, referenced to snap edge drags to beats.
    const common::core::TempoMap& m_tempo_map;

    // Visible timeline range represented by the component width.
    common::core::TimeRange m_visible_timeline{};

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

    // Region index armed by a body press, committed as a selection on click release.
    std::optional<std::size_t> m_pending_select{};
};

} // namespace rock_hero::editor::ui
