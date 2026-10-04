#include "shared/refusal_log.h"

#include <optional>
#include <rock_hero/common/core/shared/logger.h>
#include <string>
#include <string_view>
#include <utility>

namespace rock_hero::editor::core
{

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

void RefusalLog::markerEdit(const std::string_view label, const std::string_view detail)
{
    write(
        Record{
            .kind = Kind::MarkerEdit,
            .subject = std::string{label},
            .detail = std::string{detail},
        });
}

void RefusalLog::chartEdit(const std::string_view subject, const std::string_view detail)
{
    write(
        Record{
            .kind = Kind::ChartEdit,
            .subject = std::string{subject},
            .detail = std::string{detail},
        });
}

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
    }
}

} // namespace rock_hero::editor::core
