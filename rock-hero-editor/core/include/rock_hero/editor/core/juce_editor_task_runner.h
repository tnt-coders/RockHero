/*!
\file juce_editor_task_runner.h
\brief JUCE-backed editor task runner used by production composition.
*/

#pragma once

#include <functional>
#include <rock_hero/editor/core/i_editor_task_runner.h>
#include <thread>

namespace rock_hero::editor::core
{

/*!
\brief Editor task runner backed by std::thread workers and JUCE message-thread marshaling.

Submits work to a single worker thread at a time. When the worker finishes, completion is
posted to the message thread through `juce::MessageManager::callAsync()` so the controller can
apply results without touching JUCE or Tracktion state from the worker. The destructor joins any
outstanding worker before allowing the runner to be destroyed, so Exit during an in-flight
operation waits for the worker to finish before the app shuts down.

Slice 1 processes at most one in-flight open or import at a time because Close and Exit
supersede prior operations by advancing the controller's busy generation. If a later slice
introduces concurrent submissions, this runner must grow a queue.
*/
class JuceEditorTaskRunner final : public IEditorTaskRunner
{
public:
    /*! \brief Creates an idle runner with no outstanding worker. */
    JuceEditorTaskRunner() = default;

    /*! \brief Joins any outstanding worker before destruction completes. */
    ~JuceEditorTaskRunner() override;

    /*! \brief Copying is disabled because worker ownership is not copyable. */
    JuceEditorTaskRunner(const JuceEditorTaskRunner&) = delete;

    /*! \brief Copy assignment is disabled because worker ownership is not copyable. */
    JuceEditorTaskRunner& operator=(const JuceEditorTaskRunner&) = delete;

    /*! \brief Moving is disabled to keep worker ownership stable. */
    JuceEditorTaskRunner(JuceEditorTaskRunner&&) = delete;

    /*! \brief Move assignment is disabled to keep worker ownership stable. */
    JuceEditorTaskRunner& operator=(JuceEditorTaskRunner&&) = delete;

    /*!
    \brief Joins any prior worker, then submits new work for off-thread execution.

    The current worker, if any, is joined before the new worker starts so the runner holds at
    most one worker at a time. After the new worker's work returns, completion is posted to the
    message thread via `juce::MessageManager::callAsync()`.

    \param work Callable invoked on the worker thread.
    \param completion Callable invoked on the message thread after work returns.
    */
    void submit(std::function<void()> work, std::function<void()> completion) override;

private:
    // Worker that owns the in-flight work. Held as a member so the destructor can join it.
    std::thread m_worker;
};

} // namespace rock_hero::editor::core
