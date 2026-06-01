#include <algorithm>
#include <juce_events/juce_events.h>
#include <rock_hero/editor/core/juce_message_thread_scheduler.h>

namespace rock_hero::editor::core
{
bool JuceMessageThreadScheduler::postToMessageThread(std::function<void()> work)
{
    if (!work)
    {
        return false;
    }

    return juce::MessageManager::callAsync(std::move(work));
}

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
