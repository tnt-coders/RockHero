/*!
\file i_message_thread_scheduler.h
\brief Message-thread scheduling seam for editor workflows.
*/

#pragma once

#include <chrono>
#include <functional>

namespace rock_hero::editor::core
{
/// Schedules work onto the UI message thread.
class IMessageThreadScheduler
{
public:
    virtual ~IMessageThreadScheduler() = default;

    /// Posts work for asynchronous execution on the message thread.
    [[nodiscard]] virtual bool postToMessageThread(std::function<void()> work) = 0;

    /// Posts work for delayed execution on the message thread.
    [[nodiscard]] virtual bool callAfterDelay(
        std::chrono::milliseconds delay, std::function<void()> work) = 0;
};
} // namespace rock_hero::editor::core
