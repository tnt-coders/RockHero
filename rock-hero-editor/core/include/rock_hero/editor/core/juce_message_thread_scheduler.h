/*!
\file juce_message_thread_scheduler.h
\brief JUCE-backed message-thread scheduler for editor workflows.
*/

#pragma once

#include <rock_hero/editor/core/i_message_thread_scheduler.h>

namespace rock_hero::editor::core
{
/*! \brief IMessageThreadScheduler implementation backed by JUCE message-thread primitives. */
class JuceMessageThreadScheduler final : public IMessageThreadScheduler
{
public:
    /*!
    \brief Posts work for asynchronous execution through JUCE's message manager.
    \param work Callback to invoke on the message thread.
    \return True when JUCE accepted the callback for delivery.
    */
    [[nodiscard]] bool postToMessageThread(std::function<void()> work) override;

    /*!
    \brief Posts work for delayed execution through JUCE's timer queue.
    \param delay Minimum delay before the callback should run.
    \param work Callback to invoke on the message thread.
    \return True when JUCE accepted the delayed callback for delivery.
    */
    [[nodiscard]] bool callAfterDelay(
        std::chrono::milliseconds delay, std::function<void()> work) override;
};
} // namespace rock_hero::editor::core
