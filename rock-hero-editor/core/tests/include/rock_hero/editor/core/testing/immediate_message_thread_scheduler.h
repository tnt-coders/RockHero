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
    /*!
    \brief Runs posted work on the calling thread.
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
    \brief Runs delayed work on the calling thread without sleeping.
    \param delay Ignored delay value retained to match the production scheduler contract.
    \param work Callback to invoke immediately.
    \return True when a callback was supplied and invoked.
    */
    [[nodiscard]] bool callAfterDelay(
        std::chrono::milliseconds delay, std::function<void()> work) override
    {
        static_cast<void>(delay);
        if (!work)
        {
            return false;
        }

        work();
        return true;
    }
};

} // namespace rock_hero::editor::core::testing
