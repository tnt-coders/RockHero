/*!
\file refusal_log.h
\brief The one writer of the editor's refusal records, which merges a run of identical ones.
*/

#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace rock_hero::editor::core
{

/*!
\brief Writes every refused marker edit's, chart edit's or locked action's record, merging identical
consecutive records.

Verbs refuse per event, since the core sees no gesture boundary and the answer can change mid-drag,
so a refused drag or a held key repeats one record per event. A record unlike the one before it is
written at once and unchanged, so a refusal never waits to be seen. An identical record that follows
it is only counted. The run ends at the next record that differs, or when the log is destroyed with
its controller, and a run that repeated writes one more line: the record again with
`repeats=<count>` appended, the count being the records merged after the first line. Other log lines
interleave with a run (the action pipeline logs every event), so the count line names its record in
full rather than pointing at a line above it.

Owned by the controller, which refuses on the message thread only, so the run needs no lock.
*/
class RefusalLog final
{
public:
    /*! \brief Starts with no run. */
    RefusalLog() = default;

    /*! \brief Ends the open run, so a merged count is never lost on exit. */
    ~RefusalLog();

    RefusalLog(const RefusalLog&) = delete;
    RefusalLog& operator=(const RefusalLog&) = delete;
    RefusalLog(RefusalLog&&) = delete;
    RefusalLog& operator=(RefusalLog&&) = delete;

    /*!
    \brief Records a refused timeline-marker edit, whether the commit funnel refused the produced
    model or the verb refused before it had a model to hand over.
    \param label The verb's undo label, naming what was refused.
    \param detail Why it was refused.
    */
    void markerEdit(std::string_view label, std::string_view detail);

    /*!
    \brief Records a refused chart edit. The subject names WHAT, so a reason only ever says WHY.
    \param subject The notes or keyframes refused.
    \param detail Why they were refused.
    */
    void chartEdit(std::string_view subject, std::string_view detail);

    /*!
    \brief Records an action a lock refused before it ran, so no verb had yet decided what it would
    change.
    \param action The refused action's name.
    \param detail The lock's sentence.
    */
    void lockedAction(std::string_view action, std::string_view detail);

private:
    // Which record format a refusal is written in.
    enum class Kind : std::uint8_t
    {
        MarkerEdit,
        ChartEdit,
        LockedAction,
    };

    // One refusal record, compared whole to decide whether it repeats the run's.
    struct Record
    {
        Kind kind{Kind::MarkerEdit};
        std::string subject;
        std::string detail;

        friend bool operator==(const Record& lhs, const Record& rhs) = default;
    };

    // The record a run repeats and how many times it has repeated after its first line.
    struct Run
    {
        Record record;
        std::size_t repeats{0};
    };

    void write(Record record);
    void endRun();
    static void writeLine(const Record& record, std::string_view suffix);

    std::optional<Run> m_run{};
};

} // namespace rock_hero::editor::core
