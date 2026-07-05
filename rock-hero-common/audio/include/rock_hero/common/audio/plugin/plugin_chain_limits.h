/*!
\file plugin_chain_limits.h
\brief Shared product limits for user-visible plugin chains.
*/

#pragma once

#include <cstddef>
#include <string>

namespace rock_hero::common::audio
{

/*!
\brief Maximum number of user plugins currently allowed in one signal chain.

This is a product rule, not a Tracktion or UI layout limit. Keep all insertion, restore,
capture, and presentation code wired to this value so raising the limit later is localized.
*/
inline constexpr std::size_t g_max_signal_chain_plugins{8};

/*!
\brief Formats the shared signal-chain plugin limit for boundary diagnostics.
\param plugin_count Number of user plugins found in the current chain.
\return User-facing diagnostic sentence for the product cap.
*/
[[nodiscard]] inline std::string pluginChainLimitExceededMessage(std::size_t plugin_count)
{
    return "Signal chain contains " + std::to_string(plugin_count) +
           " plugins; this version supports up to " + std::to_string(g_max_signal_chain_plugins);
}

} // namespace rock_hero::common::audio
