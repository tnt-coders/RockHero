/*!
\file deferring_message_thread_scheduler.h
\brief IMessageThreadScheduler fake that queues delayed work for explicit pumping.
*/

#pragma once

#include <chrono>
#include <cstddef>
#include <functional>
#include <rock_hero/editor/core/tasks/i_message_thread_scheduler.h>
#include <utility>
#include <vector>

namespace rock_hero::editor::core::testing
{

/*!
\brief IMessageThreadScheduler implementation that runs posts inline but QUEUES delayed work.

The immediate scheduler runs callAfterDelay synchronously, which makes any state whose meaning
is "nothing has happened yet" untestable — the pending fret entry would meet its window wake
inside the very keystroke that armed it. This fake holds delayed callbacks until the test pumps
them with runDelayed(), so a test can assert the pending state, advance its injected clock, and
then deliver the wake exactly the way the production timer would.
*/
class DeferringMessageThreadScheduler final : public IMessageThreadScheduler
{
public:
    /*!
    \brief Runs posted work on the calling thread, like the immediate scheduler.
    \param work Callback to invoke immediately.
    \return True when a callback was supplied and invoked.
    */
    [[nodiscard]] bool postToMessageThread(std::function<void()> work) override
    {
        if (!work)
        {
            return false;
        }

        work();
        return true;
    }

    /*!
    \brief Queues delayed work until runDelayed() delivers it.
    \param delay Ignored: the test's injected clock is the authority over elapsed time, so the
           pump decides when the callback is due, not the requested delay.
    \param work Callback to queue.
    \return True when a callback was supplied and queued.
    */
    [[nodiscard]] bool callAfterDelay(
        std::chrono::milliseconds delay, std::function<void()> work) override
    {
        static_cast<void>(delay);
        if (!work)
        {
            return false;
        }

        m_delayed.push_back(std::move(work));
        return true;
    }

    /*!
    \brief Delivers every queued delayed callback in order.

    Drains a snapshot: a callback that schedules more delayed work leaves the new work queued
    for the next pump instead of running in the same drain, matching a real timer, which never
    fires inside the tick that scheduled it.

    \return Number of callbacks delivered.
    */
    std::size_t runDelayed()
    {
        const std::vector<std::function<void()>> due = std::move(m_delayed);
        m_delayed.clear();
        for (const std::function<void()>& work : due)
        {
            work();
        }
        return due.size();
    }

private:
    // Delayed callbacks in scheduling order, drained by runDelayed().
    std::vector<std::function<void()>> m_delayed{};
};

} // namespace rock_hero::editor::core::testing
