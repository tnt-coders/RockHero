#include "shared/refusal_log.h"

#include <optional>
#include <rock_hero/common/core/shared/logger.h>
#include <string>
#include <string_view>
#include <utility>

namespace rock_hero::editor::core
{

// Ends the open run so a merged count reaches the log on exit. A destructor must not throw, so a
// failing write drops the run instead.
RefusalLog::~RefusalLog()
{
    try
    {
        endRun();
    }
    catch (...)
    {
        m_run.reset();
    }
}

// Records a refused timeline-marker edit under the verb's undo label.
void RefusalLog::markerEdit(const std::string_view label, const std::string_view detail)
{
    write(
        Record{
            .kind = Kind::MarkerEdit,
            .subject = std::string{label},
            .detail = std::string{detail},
        });
}

// Records a refused chart edit, naming the notes or keyframes it would have changed.
void RefusalLog::chartEdit(const std::string_view subject, const std::string_view detail)
{
    write(
        Record{
            .kind = Kind::ChartEdit,
            .subject = std::string{subject},
            .detail = std::string{detail},
        });
}

// Records an action a lock refused before any verb ran, with the lock's sentence as the detail.
void RefusalLog::lockedAction(const std::string_view action, const std::string_view detail)
{
    write(
        Record{
            .kind = Kind::LockedAction,
            .subject = std::string{action},
            .detail = std::string{detail},
        });
}

// Counts a record that repeats the open run; any other record closes the run, is written at once
// and opens the next run.
void RefusalLog::write(Record record)
{
    if (m_run.has_value())
    {
        Run& run = *m_run;
        if (run.record == record)
        {
            ++run.repeats;
            return;
        }
    }
    endRun();
    writeLine(record, {});
    m_run = Run{.record = std::move(record), .repeats = 0};
}

// Closes the open run, writing its count line only when the record repeated after its first line.
void RefusalLog::endRun()
{
    const std::optional<Run> run = std::exchange(m_run, std::nullopt);
    if (run.has_value() && run->repeats > 0)
    {
        writeLine(run->record, " repeats=" + std::to_string(run->repeats));
    }
}

// Each kind's format is stated once: the count line is its record with the count appended, so the
// empty suffix writes a run's first line byte for byte as a lone refusal's.
void RefusalLog::writeLine(const Record& record, const std::string_view suffix)
{
    switch (record.kind)
    {
        case Kind::MarkerEdit:
        {
            RH_LOG_WARNING(
                "editor.marker",
                "Rejected marker edit label={:?} detail={:?}{}",
                record.subject,
                record.detail,
                suffix);
            return;
        }
        case Kind::ChartEdit:
        {
            RH_LOG_WARNING(
                "editor.chart",
                "Refused chart edit subject={:?} detail={:?}{}",
                record.subject,
                record.detail,
                suffix);
            return;
        }
        case Kind::LockedAction:
        {
            RH_LOG_WARNING(
                "editor.controller",
                "Refused locked action action={:?} detail={:?}{}",
                record.subject,
                record.detail,
                suffix);
            return;
        }
    }
}

} // namespace rock_hero::editor::core
