/*!
\file juce_message_pump.h
\brief Bounded, non-blocking drain of JUCE's pending message queue for the game frame loop.
*/

#pragma once

namespace rock_hero::game::ui
{

/*!
\brief Dispatches up to max_messages pending JUCE message-queue callbacks without blocking.

Loop model L2's per-frame JUCE service point: the game loop calls this once per frame between
event polling and frame submission, so queued JUCE callbacks (timers, async calls, Tracktion's
state sync) wait at most one frame. Returns early as soon as the queue is empty.

\param max_messages Upper bound on callbacks dispatched by this call.
*/
void drainPendingJuceMessages(int max_messages);

} // namespace rock_hero::game::ui
