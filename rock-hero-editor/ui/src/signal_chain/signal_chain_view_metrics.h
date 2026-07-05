/*!
\file signal_chain_view_metrics.h
\brief Fixed block and tile pixel metrics shared by the signal-chain view and its tiles.
*/

#pragma once

namespace rock_hero::editor::ui
{

/*! \brief Width of the compact plugin block square. */
constexpr int g_signal_block_width{70};

/*! \brief Height of the compact plugin block square. */
constexpr int g_signal_block_height{64};

/*! \brief Side length of the icon painted inside a plugin block. */
constexpr int g_signal_block_icon_size{44};

/*! \brief Gap between the plugin block and its caption lines. */
constexpr int g_signal_block_label_gap{6};

/*! \brief Height of the plugin-name caption line. */
constexpr int g_signal_block_name_height{18};

/*! \brief Height of the manufacturer caption line. */
constexpr int g_signal_block_maker_height{15};

/*! \brief Width of one plugin block-plus-caption view. */
constexpr int g_signal_plugin_view_width{118};

/*! \brief Height of one plugin block-plus-caption view. */
constexpr int g_signal_plugin_view_height{
    g_signal_block_height + g_signal_block_label_gap + g_signal_block_name_height +
    g_signal_block_maker_height
};

} // namespace rock_hero::editor::ui
