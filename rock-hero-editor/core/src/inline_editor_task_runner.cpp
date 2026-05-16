#include "inline_editor_task_runner.h"

namespace rock_hero::editor::core
{

// Runs work, then completion, on the calling thread. Headless tests rely on this so the
// controller's open/import flow finishes before submit() returns.
void InlineEditorTaskRunner::submit(std::function<void()> work, std::function<void()> completion)
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

} // namespace rock_hero::editor::core
