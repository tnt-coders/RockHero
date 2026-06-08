/*!
\file cancellation_token.h
\brief Project-owned cooperative cancellation handle shared across worker boundaries.
*/

#pragma once

#include <atomic>
#include <memory>

namespace rock_hero::common::core
{

/*!
\brief Copyable handle used to request and observe cooperative cancellation of background work.

The handle wraps a shared atomic flag. The owner that started the work calls cancel() from the
message thread; the worker polls isCancelled() at safe checkpoints and stops promptly when set.
Copies share the same flag, so a worker closure can capture a copy and still observe cancellation
requested through the original handle.

This is a cooperation primitive, not a thread synchronization barrier: it only signals intent. It
does not interrupt a call already executing inside a checkpoint, and it makes no ordering
guarantees beyond the relaxed atomic flag itself.
*/
class CancellationToken
{
public:
    /*! \brief Creates a token whose shared flag starts uncancelled. */
    CancellationToken() = default;

    /*! \brief Requests cancellation for every handle sharing this token's flag. */
    void cancel() noexcept
    {
        m_flag->store(true, std::memory_order_relaxed);
    }

    /*!
    \brief Reports whether cancellation has been requested.
    \return True once cancel() has been called on any handle sharing this flag.
    */
    [[nodiscard]] bool isCancelled() const noexcept
    {
        return m_flag->load(std::memory_order_relaxed);
    }

private:
    std::shared_ptr<std::atomic<bool>> m_flag{std::make_shared<std::atomic<bool>>(false)};
};

} // namespace rock_hero::common::core
