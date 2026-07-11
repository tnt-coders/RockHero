/*!
\file bgfx_handle.h
\brief Move-only RAII ownership of one bgfx handle, destroyed on scope exit.
*/

#pragma once

#include <bgfx/bgfx.h>
#include <utility>

namespace rock_hero::common::ui
{

/*!
\brief Unique ownership of one bgfx handle: move-only, destroys on scope exit.

bgfx handles are per-create-call refcounted, so one create must pair with exactly one destroy —
which is unique-ownership semantics; this wrapper makes the destroy structural instead of a
manual obligation on every exit path. Two constraints its users own:

- Every instance must die before bgfx::shutdown(); destroying a handle after shutdown
  dereferences bgfx's nulled context. In this project all handles live under RenderDevice, which
  owns shutdown, so the ordering is structural — but never make one of these a member of the type
  whose destructor body calls shutdown (member destructors run after the body).
- Never pass ownership-consuming flags (createProgram's destroyShaders, createFrameBuffer's
  destroyTextures) alongside wrapped handles: bgfx consumes the inputs on some failure paths but
  not others, so pass false and let the wrapper destroy unconditionally — on success the created
  object holds its own references.
*/
template <typename HandleT> class [[nodiscard]] UniqueBgfxHandle
{
public:
    /*! \brief Creates an empty wrapper holding bgfx's invalid-handle sentinel. */
    UniqueBgfxHandle() noexcept = default;

    /*!
    \brief Takes ownership of a handle (which may be invalid, leaving the wrapper empty).
    \param handle Handle to own.
    */
    explicit UniqueBgfxHandle(const HandleT handle) noexcept
        : m_handle{handle}
    {}

    /*! \brief Destroys the owned handle when one is held. */
    ~UniqueBgfxHandle()
    {
        reset();
    }

    /*!
    \brief Steals ownership; the source is left empty.
    \param other Wrapper losing ownership.
    */
    UniqueBgfxHandle(UniqueBgfxHandle&& other) noexcept
        : m_handle{std::exchange(other.m_handle, HandleT{bgfx::kInvalidHandle})}
    {}

    /*!
    \brief Destroys the currently owned handle, then steals ownership; the source is left empty.
    \param other Wrapper losing ownership.
    \return This wrapper.
    */
    UniqueBgfxHandle& operator=(UniqueBgfxHandle&& other) noexcept
    {
        if (this != &other)
        {
            reset();
            m_handle = std::exchange(other.m_handle, HandleT{bgfx::kInvalidHandle});
        }
        return *this;
    }

    UniqueBgfxHandle(const UniqueBgfxHandle&) = delete;
    UniqueBgfxHandle& operator=(const UniqueBgfxHandle&) = delete;

    /*!
    \brief Returns the owned handle for bgfx calls; ownership stays with the wrapper.
    \return The owned handle (possibly the invalid sentinel).
    */
    [[nodiscard]] HandleT get() const noexcept
    {
        return m_handle;
    }

    /*!
    \brief Whether a valid handle is owned.
    \return True when the handle is valid.
    */
    [[nodiscard]] bool isValid() const noexcept
    {
        return bgfx::isValid(m_handle);
    }

    /*! \brief Destroys the owned handle when one is held and leaves the wrapper empty. */
    void reset() noexcept
    {
        // The validity guard is load-bearing: bgfx::destroy on the invalid sentinel asserts in
        // debug builds and is out-of-bounds UB in release for several handle types.
        if (bgfx::isValid(m_handle))
        {
            bgfx::destroy(m_handle);
        }
        m_handle = HandleT{bgfx::kInvalidHandle};
    }

private:
    // Owned handle; bgfx's invalid sentinel marks the empty state.
    HandleT m_handle{bgfx::kInvalidHandle};
};

} // namespace rock_hero::common::ui
