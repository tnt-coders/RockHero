/*!
\file i_editor_task_runner.h
\brief Project-owned boundary for running editor background work and marshaling completion.
*/

#pragma once

#include <functional>

namespace rock_hero::editor::core
{

/*!
\brief Runs slow editor work off the message thread.

Submits a work callable to be run on a worker thread. When the work returns, the runner schedules
the completion callable on the message thread so the controller can apply results without
touching JUCE or Tracktion state from the worker.

Implementations must join any outstanding worker in their destructor so the editor task runner
outlives no worker thread that captured its surrounding state. Callers are responsible for
guarding completions against stale state through the controller's current busy token.
*/
class IEditorTaskRunner
{
public:
    /*! \brief Destroys the task-runner interface. */
    virtual ~IEditorTaskRunner() = default;

    /*!
    \brief Submits work for off-thread execution with on-message-thread completion.

    \param work Callable invoked on a worker thread. Must not touch controller, session, UI, or
    message-thread-only audio state. It may call project-owned ports whose contracts explicitly
    allow worker-thread use. The runner moves this callable into worker storage.

    \param completion Callable invoked on the message thread after work returns. The runner moves
    this callable into completion storage. The caller is responsible for any stale-completion or
    lifetime guards needed before touching surrounding state.
    */
    virtual void submit(std::function<void()> work, std::function<void()> completion) = 0;

protected:
    /*! \brief Creates the task-runner interface. */
    IEditorTaskRunner() = default;

    /*! \brief Copies the task-runner interface. */
    IEditorTaskRunner(const IEditorTaskRunner&) = default;

    /*! \brief Moves the task-runner interface. */
    IEditorTaskRunner(IEditorTaskRunner&&) = default;

    /*!
    \brief Assigns the task-runner interface from another interface.
    \return Reference to this task-runner interface.
    */
    IEditorTaskRunner& operator=(const IEditorTaskRunner&) = default;

    /*!
    \brief Move-assigns the task-runner interface from another interface.
    \return Reference to this task-runner interface.
    */
    IEditorTaskRunner& operator=(IEditorTaskRunner&&) = default;
};

} // namespace rock_hero::editor::core
