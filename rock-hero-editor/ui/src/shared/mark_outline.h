/*!
\file mark_outline.h
\brief The one ring idiom every editor overlay traces a mark with: its closed outline, the stroke
its selection ring wears, and the span row's fused chip-and-rail outline.
*/

#pragma once

#include <juce_graphics/juce_graphics.h>

namespace rock_hero::editor::ui
{

/*!
\brief A mark an overlay traces: its closed outline, and the size its ring and glow scale with.

The selection ring strokes the path (\ref strokeMarkRing) and the refusal glow spreads outward from
it (paintRefusalGlow), so the two can never disagree about a mark's shape.
*/
struct MarkOutline
{
    /*! \brief The mark's closed outline. */
    juce::Path path;

    /*! \brief The mark's size: the ring's stroke scales with it (\ref overlayRingStroke). */
    float extent{0.0f};
};

/*!
\brief The one stroke width of every overlay outline — the selection rings and the caret square —
so the editing furniture reads as one family at any mark size.
\param extent The traced mark's size (a head's, or a box's height).
\return The stroke width in pixels: one and a half border widths, never under 1.5.
*/
[[nodiscard]] float overlayRingStroke(float extent) noexcept;

/*!
\brief The outline of a BOXED mark — a chip, a rail — lying just outside the box and following its
corners, so the ring stroked on it lies wholly outside the box and covers none of its fill.
\param box The mark's box.
\param corner_radius The box's own corner radius; zero for a square box.
\return The box grown by half the ring's stroke, its corners rounded by the box's radius plus that
        half, with the box's height as its extent.
*/
[[nodiscard]] MarkOutline boxRingOutline(juce::Rectangle<float> box, float corner_radius);

/*!
\brief The ONE closed outline around a span's chip and its top rail, which read as one shape: the
chip stands square on the rail with their left edges shared.

Both boxes grow by half the ring's stroke and the outline steps between them: from the left edge
along the chip's top, down its right side to the rail, out along the rail's top to its right end
(or, where the rail is the narrower — clipped at the view's edge — along the chip's base back to the
rail), down to the rail's base and back. Convex corners round as \ref boxRingOutline rounds them (a
chip's top corners by the chip's radius); the concave corner where chip meets rail stays square. A
step narrower than the ring's own stroke would read as a jog, so the two right edges then run as one
at the wider. A bounding box would be wrong: it would ring the empty band beside the chip.

Where either box is empty the outline is the other's \ref boxRingOutline.

\param chip The chip's box, empty where the span shows no chip.
\param top_rail The top rail's visible box, empty where none of it shows.
\return The outline, with the rail's height as its extent (the chip's where there is no rail).
*/
[[nodiscard]] MarkOutline spanRowOutline(
    juce::Rectangle<float> chip, juce::Rectangle<float> top_rail);

/*!
\brief Strokes a mark's selection ring on its outline at the overlay stroke.
\param g Graphics context to paint into.
\param mark The mark's outline.
\param colour The ring's colour.
*/
void strokeMarkRing(juce::Graphics& g, const MarkOutline& mark, juce::Colour colour);

} // namespace rock_hero::editor::ui
