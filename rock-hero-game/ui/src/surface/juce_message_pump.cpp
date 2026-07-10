#include "surface/juce_message_pump.h"

// JUCE ships no public header for its non-blocking dispatch primitive; JUCE's own cross-module
// consumers forward-declare it exactly like this (juce_FileChooser_windows.cpp), which is what
// makes the contract dependable. Keep this the project's only declaration so a JUCE-upgrade
// break surfaces here and nowhere else.
namespace juce::detail
{
bool dispatchNextMessageOnSystemQueue(bool return_if_no_pending_messages);
} // namespace juce::detail

namespace rock_hero::game::ui
{

// Dispatches pending JUCE callbacks until the queue is empty or the per-call bound is reached.
// The bound exists to keep a pathological self-posting message burst from starving the frame;
// the gate soak measured a real-world per-frame maximum of 3 messages.
void drainPendingJuceMessages(const int max_messages)
{
    for (int dispatched = 0; dispatched < max_messages; ++dispatched)
    {
        if (!juce::detail::dispatchNextMessageOnSystemQueue(true))
        {
            break;
        }
    }
}

} // namespace rock_hero::game::ui
