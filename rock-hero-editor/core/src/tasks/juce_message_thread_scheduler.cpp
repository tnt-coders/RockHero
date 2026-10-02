#include <algorithm>
#include <juce_events/juce_events.h>
#include <rock_hero/editor/core/tasks/juce_message_thread_scheduler.h>

namespace rock_hero::editor::core
{
// Rejects an empty callback with false rather than posting a call that would have nothing to run.
bool JuceMessageThreadScheduler::postToMessageThread(std::function<void()> work)
{
    if (!work)
    {
        return false;
    }

    return juce::MessageManager::callAsync(std::move(work));
}

// Clamps a negative delay to zero because juce::Timer::callAfterDelay takes a plain int, and
// reports acceptance unconditionally since that JUCE call has no failure result.
bool JuceMessageThreadScheduler::callAfterDelay(
    std::chrono::milliseconds delay, std::function<void()> work)
{
    if (!work)
    {
        return false;
    }

    const auto delay_ms = std::max(0, static_cast<int>(delay.count()));
    juce::Timer::callAfterDelay(delay_ms, std::move(work));
    return true;
}
} // namespace rock_hero::editor::core
