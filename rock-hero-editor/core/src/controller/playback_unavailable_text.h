/*!
\file playback_unavailable_text.h
\brief English tooltip strings for a refused transport.
*/

#pragma once

#include "editor_action_availability.h"

#include <string>

namespace rock_hero::editor::core
{

/*!
\brief Returns the English text that says why Play and Stop are unavailable.

Worded in the signal chain's "X disabled: reason." style, and naming the condition rather than
its fix, so it stays true wherever it is shown.

\param reason Reason the transport's shared gate refused.
\return Tooltip and log text for the refused transport.
*/
[[nodiscard]] std::string playbackUnavailableText(ActionUnavailableReason reason);

} // namespace rock_hero::editor::core
