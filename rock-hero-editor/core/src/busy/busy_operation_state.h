/*!
\file busy_operation_state.h
\brief Headless editor busy-operation state machine.
*/

#pragma once

#include <cstdint>
#include <optional>
#include <rock_hero/editor/core/busy/busy_view_state.h>
#include <string>

namespace rock_hero::editor::core
{

/*!
\brief Owns the active editor busy operation and its async invalidation token.

The root editor controller uses this state machine to decide whether async completions are still
current and to project a BusyViewState for the view. It does not submit work, wait for paint
callbacks, call view APIs, or own any editor ports.
*/
class BusyOperationState final
{
public:
    /*!
    \brief Reports whether an operation is currently active.
    \return True when a busy operation is active.
    */
    [[nodiscard]] bool isBusy() const noexcept;

    /*!
    \brief Starts a new busy operation and invalidates previous tokens.
    \param operation Operation that is now active.
    \return Token identifying this operation generation.
    */
    [[nodiscard]] std::uint64_t begin(BusyOperation operation) noexcept;

    /*!
    \brief Reports whether a token still matches the current operation generation.
    \param token Token captured when an operation began.
    \return True when the token matches the current generation.
    */
    [[nodiscard]] bool isCurrentToken(std::uint64_t token) const noexcept;

    /*!
    \brief Returns the current busy token generation.
    \return Current token value.
    */
    [[nodiscard]] std::uint64_t currentToken() const noexcept;

    /*!
    \brief Transitions the active operation without changing its token.
    \param operation Operation phase to show next.
    \param token Token that must match the current generation.
    \return True when the transition was applied.
    */
    [[nodiscard]] bool transition(BusyOperation operation, std::uint64_t token) noexcept;

    /*! \brief Clears the active operation and invalidates pending completions. */
    void end() noexcept;

    /*!
    \brief Switches the active operation into determinate live-rig load progress.
    \return True when progress state was applied.
    */
    [[nodiscard]] bool beginLiveRigLoadProgress();

    /*!
    \brief Updates the live-rig load progress payload.
    \param message User-facing progress message.
    \param fraction Determinate progress fraction.
    \return True when the progress state was applied.
    */
    [[nodiscard]] bool setLiveRigLoadProgress(std::string message, double fraction);

    /*!
    \brief Updates the plugin catalog scan progress payload.
    \param message User-facing progress message.
    \param fraction Determinate progress fraction.
    \return True when the progress state was applied.
    */
    [[nodiscard]] bool setPluginCatalogScanProgress(std::string message, double fraction);

    /*!
    \brief Builds the current busy view-state snapshot.
    \return Busy view state, or empty when no operation is active.
    */
    [[nodiscard]] std::optional<BusyViewState> viewState() const;

private:
    struct DeterminateProgress
    {
        std::string message;
        double fraction;
    };

    std::optional<BusyOperation> m_operation{};
    std::optional<DeterminateProgress> m_determinate_progress{};
    std::uint64_t m_current_token{0};
};

} // namespace rock_hero::editor::core
