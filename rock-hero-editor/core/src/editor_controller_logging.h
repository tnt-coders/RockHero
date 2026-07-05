/*!
ile editor_controller_logging.h
rief Shared best-effort failure logging for the editor controller's translation units.
*/

#pragma once

#include <rock_hero/common/core/logger.h>
#include <string>
#include <string_view>

namespace rock_hero::editor::core
{

/*!
rief Routes non-fatal cleanup/persistence failures to the debug log.

Keeps the primary workflow result being handled by the caller visible while still recording the
secondary failure. Shared by the controller's per-feature translation units.

\param context Short caller-supplied context tag naming the best-effort operation.
\param message Failure detail from the underlying error.
*/
inline void logEditorControllerBestEffortFailure(
    std::string_view context, const std::string& message)
{
    RH_LOG_WARNING(
        "editor.controller",
        "Best-effort cleanup or persistence failed context={:?} detail={:?}",
        context,
        message);
}

} // namespace rock_hero::editor::core
