/*!
\file i_message_thread_scheduler.h
\brief Message-thread scheduling seam for editor workflows.
*/

#pragma once

#include <chrono>
#include <functional>

namespace rock_hero::editor::core
{
/*! \brief Schedules work onto the UI message thread. */
class IMessageThreadScheduler
{
public:
    /*! \brief Destroys the scheduler interface. */
    virtual ~IMessageThreadScheduler() = default;

    /*!
    \brief Posts work for asynchronous execution on the message thread.
    \param work Callback to invoke on the message thread.
    \return True when the callback was accepted for delivery.
    */
    [[nodiscard]] virtual bool postToMessageThread(std::function<void()> work) = 0;

    /*!
    \brief Posts work for delayed execution on the message thread.
    \param delay Minimum delay before the callback should run.
    \param work Callback to invoke on the message thread.
    \return True when the delayed callback was accepted for delivery.
    */
    [[nodiscard]] virtual bool callAfterDelay(
        std::chrono::milliseconds delay, std::function<void()> work) = 0;

protected:
    /*! \brief Creates the scheduler interface. */
    IMessageThreadScheduler() = default;

    /*! \brief Copies the scheduler interface. */
    IMessageThreadScheduler(const IMessageThreadScheduler&) = default;

    /*! \brief Moves the scheduler interface. */
    IMessageThreadScheduler(IMessageThreadScheduler&&) = default;

    /*!
    \brief Assigns the scheduler interface.
    \return Reference to this scheduler interface.
    */
    IMessageThreadScheduler& operator=(const IMessageThreadScheduler&) = default;

    /*!
    \brief Move-assigns the scheduler interface.
    \return Reference to this scheduler interface.
    */
    IMessageThreadScheduler& operator=(IMessageThreadScheduler&&) = default;
};
} // namespace rock_hero::editor::core
