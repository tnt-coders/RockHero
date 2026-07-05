/*!
\file immediate_editor_task_runner.h
\brief Synchronous editor task runner for tests that do not need deferred completion.
*/

#pragma once

#include <functional>
#include <rock_hero/editor/core/tasks/i_editor_task_runner.h>

namespace rock_hero::editor::core::testing
{

/*!
\brief IEditorTaskRunner implementation that runs work and completion immediately.

Use this when a test needs deterministic editor-controller construction and does not need to
observe background completion ordering. Tests that exercise stale completions or interleaved async
work should use a deferred runner instead.
*/
class ImmediateEditorTaskRunner final : public IEditorTaskRunner
{
public:
    /*!
    \brief Runs submitted work and completion on the calling thread.
    \param work Work callback normally run by a background worker.
    \param completion Completion callback normally posted back to the message thread.
    */
    void submit(std::function<void()> work, std::function<void()> completion) override
    {
        if (work)
        {
            work();
        }

        if (completion)
        {
            completion();
        }
    }
};

} // namespace rock_hero::editor::core::testing
