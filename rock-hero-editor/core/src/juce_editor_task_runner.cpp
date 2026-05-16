#include "juce_editor_task_runner.h"

#include <juce_events/juce_events.h>
#include <utility>

namespace rock_hero::editor::core
{

// Joins the outstanding worker so any callAsync completion posts have been queued before the
// runner is destroyed. The captured completion may still fire after this destructor returns if
// the MessageManager is alive; callers guard their completions against stale state through the
// controller's busy generation token, not through runner-level lifetime tracking.
JuceEditorTaskRunner::~JuceEditorTaskRunner()
{
    if (m_worker.joinable())
    {
        m_worker.join();
    }
}

// Joins any prior worker, then spawns a new worker that runs work and posts completion to the
// message thread through juce::MessageManager::callAsync. Holding only one worker at a time is
// fine for Slice 1: Close and Exit supersede in-flight operations by advancing the controller's
// generation token, and the controller does not submit a new open or import while a prior one
// is still in flight without an intervening supersede.
void JuceEditorTaskRunner::submit(std::function<void()> work, std::function<void()> completion)
{
    if (m_worker.joinable())
    {
        m_worker.join();
    }

    m_worker = std::thread([work = std::move(work), completion = std::move(completion)]() mutable {
        if (work)
        {
            work();
        }

        if (completion)
        {
            juce::MessageManager::callAsync(std::move(completion));
        }
    });
}

} // namespace rock_hero::editor::core
