/*!
\file inline_editor_task_runner.h
\brief Synchronous editor task runner used as the default and for headless tests.
*/

#pragma once

#include <functional>
#include <rock_hero/editor/core/i_editor_task_runner.h>

namespace rock_hero::editor::core
{

/*!
\brief Editor task runner that runs work and completion inline on the calling thread.

Used as the default implementation when no explicit task runner is composed, including by
headless controller tests that should not start worker threads. Production composition replaces
this with a JUCE-backed runner that delivers off-thread execution.
*/
class InlineEditorTaskRunner final : public IEditorTaskRunner
{
public:
    /*! \brief Creates the inline task runner. */
    InlineEditorTaskRunner() = default;

    /*! \brief Destroys the inline task runner. */
    ~InlineEditorTaskRunner() override = default;

    /*! \brief Copying is disabled to keep the runner identity stable across the controller. */
    InlineEditorTaskRunner(const InlineEditorTaskRunner&) = delete;

    /*! \brief Copy assignment is disabled to keep the runner identity stable. */
    InlineEditorTaskRunner& operator=(const InlineEditorTaskRunner&) = delete;

    /*! \brief Moving is disabled to keep the runner identity stable across the controller. */
    InlineEditorTaskRunner(InlineEditorTaskRunner&&) = delete;

    /*! \brief Move assignment is disabled to keep the runner identity stable. */
    InlineEditorTaskRunner& operator=(InlineEditorTaskRunner&&) = delete;

    /*!
    \brief Runs work and then completion on the calling thread.

    Tests use this runner so the controller's open/import path completes before submit() returns,
    which keeps test assertions simple. Production code should not rely on the inline runner
    because it provides no off-thread execution.

    \param work Callable to run synchronously.
    \param completion Callable to run after work returns.
    */
    void submit(std::function<void()> work, std::function<void()> completion) override;
};

} // namespace rock_hero::editor::core
