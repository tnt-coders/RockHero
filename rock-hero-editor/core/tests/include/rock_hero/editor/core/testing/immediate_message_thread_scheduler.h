/*!
\file immediate_message_thread_scheduler.h
\brief Synchronous message-thread scheduler for editor-core tests.
*/

#pragma once

#include <chrono>
#include <functional>
#include <rock_hero/editor/core/i_message_thread_scheduler.h>

namespace rock_hero::editor::core::testing
{

/*!
\brief IMessageThreadScheduler implementation that runs scheduled work immediately.

Use this for single-threaded tests that need busy-presentation callbacks to resolve inline.
*/
class ImmediateMessageThreadScheduler final : public IMessageThreadScheduler
{
public:
    /*! \brief Runs posted work on the calling thread. */
    [[nodiscard]] bool postToMessageThread(std::function<void()> work) override
    {
        if (!work)
        {
            return false;
        }

        work();
        return true;
    }

    /*! \brief Runs delayed work on the calling thread without sleeping. */
    [[nodiscard]] bool callAfterDelay(
        std::chrono::milliseconds /*delay*/, std::function<void()> work) override
    {
        if (!work)
        {
            return false;
        }

        work();
        return true;
    }
};

} // namespace rock_hero::editor::core::testing
