/*!
\file diagnostics.h
\brief Headless dev-diagnostics state, typed intents, and the chart-source change detector.
*/

#pragma once

#include <chrono>
#include <cstddef>
#include <optional>
#include <variant>
#include <vector>

namespace rock_hero::game::core
{

/*!
\brief Presentation state of the dev-diagnostics layer.

The layer is compiled into every build and activated by the runtime dev flag (plan 20 open
question 5, answer A), so release-build timing bugs stay observable while players never see it.
*/
struct DiagnosticsState
{
    /*! \brief True when the diagnostics layer is active at all (the --dev runtime flag). */
    bool dev_mode{false};

    /*! \brief True when the diagnostics overlay draws over the scene. */
    bool overlay_visible{false};

    /*! \brief True when the autoplay stub is switched on (plan 23's bot supplies the behavior). */
    bool autoplay_enabled{false};
};

/*! \brief Requests reprojecting the loaded chart from its on-disk source. */
struct ReloadChartIntent
{
};

/*! \brief Requests seeking playback to the start of one chart section. */
struct SeekToSectionIntent
{
    /*! \brief Index into the highway view state's section list. */
    std::size_t section_index{0};
};

/*!
\brief One requested diagnostics side effect.

Intents separate the headless state machine from execution: the controller only records what was
asked for, and the shell that owns the session and renderer performs it (explicit state plus
requested side effects, per the architectural principles).
*/
using DiagnosticsIntent = std::variant<ReloadChartIntent, SeekToSectionIntent>;

/*!
\brief Owns the diagnostics state and queues the side effects dev input requests.

Pure and framework-free: inputs arrive as method calls (key toggles, watcher hits), outputs are
the state value plus a drainable intent queue. Every mutation is a no-op outside dev mode, so the
runtime activation policy is enforced here once rather than at each call site.
*/
class DiagnosticsController
{
public:
    /*!
    \brief Creates the controller; in dev mode the overlay starts visible.
    \param dev_mode True when the process runs with the dev flag.
    */
    explicit DiagnosticsController(bool dev_mode);

    /*!
    \brief Current diagnostics state for this frame.
    \return The state value consumers render from.
    */
    [[nodiscard]] const DiagnosticsState& state() const noexcept;

    /*! \brief Flips overlay visibility (dev mode only). */
    void toggleOverlay();

    /*! \brief Flips the autoplay stub flag (dev mode only). */
    void toggleAutoplay();

    /*! \brief Queues a chart-reload intent (dev mode only). */
    void requestChartReload();

    /*!
    \brief Queues a seek intent to the given section (dev mode only).
    \param section_index Index into the view state's section list.
    */
    void requestSeekToSection(std::size_t section_index);

    /*!
    \brief Drains the queued intents in request order.
    \return Every intent queued since the previous drain.
    */
    [[nodiscard]] std::vector<DiagnosticsIntent> takePendingIntents();

private:
    DiagnosticsState m_state;
    std::vector<DiagnosticsIntent> m_pending_intents;
};

/*!
\brief Debounces chart-source change stamps into reload signals.

Pure: the caller samples the source's modification stamp (any monotonic-per-source value — the
file write time in practice) and a monotonic now, so tests drive it with synthetic values. A
change is reported only after the stamp has stayed stable for the settle interval, because
editors and package writers touch files several times per save; the first sample primes the
baseline without reporting (the initial load already consumed that content).
*/
class ChartSourceWatcher
{
public:
    /*!
    \brief Creates a watcher.
    \param settle_interval How long a changed stamp must hold before a reload is reported.
    */
    explicit ChartSourceWatcher(
        std::chrono::nanoseconds settle_interval = std::chrono::milliseconds{250});

    /*!
    \brief Feeds one observation; reports whether a settled change occurred.

    The stamp and \p now may use different epochs: stamps are only compared with each other, and
    \p now only drives the settle timing. An empty stamp (source briefly missing during an atomic
    replace, probe failure) is ignored without disturbing the pending state.

    \param source_stamp Modification stamp of the watched source, when observable.
    \param now Monotonic timestamp of this observation.
    \return True exactly once per settled content change.
    */
    [[nodiscard]] bool update(
        std::optional<std::chrono::nanoseconds> source_stamp, std::chrono::nanoseconds now);

private:
    std::chrono::nanoseconds m_settle_interval;

    // Stamp of the content the consumer currently holds; empty until the first observation.
    std::optional<std::chrono::nanoseconds> m_baseline_stamp;

    // A changed stamp waiting out the settle interval, with the time it was last seen changing.
    std::optional<std::chrono::nanoseconds> m_pending_stamp;
    std::chrono::nanoseconds m_pending_since{0};
};

} // namespace rock_hero::game::core
