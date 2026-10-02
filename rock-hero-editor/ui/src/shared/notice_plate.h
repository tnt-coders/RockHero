/*!
\file notice_plate.h
\brief The plate a notice sits on when it explains why something under it is unavailable.
*/

#pragma once

#include <juce_graphics/juce_graphics.h>

namespace rock_hero::editor::ui
{

/*!
\brief Paints a notice centred in an area, on a plate the colour of the panel header, sized to the
text.

The one look for "this is unavailable, and here is why": the signal chain draws it over its dimmed
chain, and the editor over its dimmed whole while a modal window holds it. The plate is drawn
without a scrim, so the caller dims whatever the notice covers.
\param g Graphics context to draw into.
\param area The area to centre the plate in; the plate never grows wider than it.
\param text The notice.
*/
void paintNoticePlate(juce::Graphics& g, juce::Rectangle<int> area, const juce::String& text);

} // namespace rock_hero::editor::ui
