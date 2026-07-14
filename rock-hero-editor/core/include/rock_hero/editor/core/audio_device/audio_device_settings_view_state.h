/*!
\file audio_device_settings_view_state.h
\brief Editor render state for audio-device settings.
*/

#pragma once

#include <string>
#include <vector>

namespace rock_hero::editor::core
{

/*! \brief Editor-specific state rendered by the audio-device settings view. */
struct AudioDeviceSettingsViewState
{
    /*! \brief A stable one-based UI choice ID and its display text. */
    struct Choice
    {
        /*! \brief Stable one-based choice ID emitted back to the controller. */
        int id{};

        /*! \brief Display text shown to the user. */
        std::string label{};
    };

    /*! \brief Audio-system choices in display order. */
    std::vector<Choice> audio_systems{};

    /*! \brief Selected audio-system choice ID, or zero when none is selected. */
    int selected_audio_system_id{};

    /*! \brief True when the selected system uses separate input/output device rows. */
    bool uses_separate_input_output_devices{};

    /*! \brief Combined device choices for single-route audio systems. */
    std::vector<Choice> devices{};

    /*! \brief Selected combined-device choice ID, or zero when none is selected. */
    int selected_device_id{};

    /*! \brief Input device choices for split-route audio systems. */
    std::vector<Choice> input_devices{};

    /*! \brief Selected input-device choice ID, or zero when none is selected. */
    int selected_input_device_id{};

    /*! \brief Output device choices for split-route audio systems. */
    std::vector<Choice> output_devices{};

    /*! \brief Selected output-device choice ID, or zero when none is selected. */
    int selected_output_device_id{};

    /*! \brief Mono input channel choices. */
    std::vector<Choice> input_channels{};

    /*! \brief Selected input-channel choice ID, or zero when none is selected. */
    int selected_input_channel_id{};

    /*! \brief Stereo output-pair choices. */
    std::vector<Choice> stereo_output_pairs{};

    /*! \brief Selected stereo-output-pair choice ID, or zero when none is selected. */
    int selected_stereo_output_pair_id{};

    /*! \brief Sample-rate choices. */
    std::vector<Choice> sample_rates{};

    /*! \brief Selected sample-rate choice ID, or zero when none is selected. */
    int selected_sample_rate_id{};

    /*! \brief Buffer-size choices. */
    std::vector<Choice> buffer_sizes{};

    /*! \brief Selected buffer-size choice ID, or zero when none is selected. */
    int selected_buffer_size_id{};

    /*! \brief True when the staged route's audio backend exposes a control panel. */
    bool control_panel_supported{};

    /*!
    \brief True when the staged selection's driver failed to initialize.

    The driver still claims a control panel but showing it silently does nothing (for ASIO:
    hardware not connected, or the device held by another application), so the control panel
    button renders disabled with an explanatory tooltip instead of enabled-but-inert.
    */
    bool staged_device_unavailable{};

    /*! \brief True when OK should attempt to apply the staged route. */
    bool ok_enabled{};

    /*! \brief Current settings error text, or empty when no error is active. */
    std::string error_message{};
};

} // namespace rock_hero::editor::core
