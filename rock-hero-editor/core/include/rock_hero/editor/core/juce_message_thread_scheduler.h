/*!
\file juce_message_thread_scheduler.h
\brief JUCE-backed message-thread scheduler for editor workflows.
*/

#pragma once

#include <rock_hero/editor/core/i_message_thread_scheduler.h>

namespace rock_hero::editor::core
{
/// IMessageThreadScheduler implementation backed by JUCE message-thread primitives.
class JuceMessageThreadScheduler final : public IMessageThreadScheduler
{
public:
    [[nodiscard]] bool postToMessageThread(std::function<void()> work) override;
    [[nodiscard]] bool callAfterDelay(
        std::chrono::milliseconds delay, std::function<void()> work) override;
};
} // namespace rock_hero::editor::core
