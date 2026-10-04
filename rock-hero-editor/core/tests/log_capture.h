#pragma once

#include <cstdint>
#include <string>

namespace rock_hero::editor::core::testing
{

/*
The one log this test binary writes, read by every case that asserts a logged line.

The logging backend is a process singleton, and a ONE-SHOT one: Logger::shutdown is terminal, and an
init after it fails with AlreadyShutDown. Every case that reads the log therefore shares this one
rather than starting and stopping its own: ctest gives each Catch2 case its own process, but the
agent build helper runs the executable directly, where all cases share one, and a second case's init
would fail behind the first case's shutdown.

The first capture starts the backend on a file of this process's own, so the test processes ctest
runs side by side never write one file. The run's end stops the backend, as the editor's main does
at exit, and the process's exit removes the file.
*/
class LogCapture final
{
public:
    /*! \brief Starts the process's log on first use and marks where this capture begins. */
    LogCapture();

    /*!
    \brief Everything logged in this process since the capture began, flushed to the file first.

    Only this capture's own records, so a line an earlier case logged can never satisfy a search.

    \return The text the log file gained since construction.
    */
    [[nodiscard]] std::string text() const;

private:
    // The log file's size when the capture began.
    std::uintmax_t m_start{0};
};

} // namespace rock_hero::editor::core::testing
