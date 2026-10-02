/*!
\file audio_level_meter.h
\brief Compact JUCE peak meter used by editor audio panels.
*/

#pragma once

#include <cstdint>
#include <juce_gui_basics/juce_gui_basics.h>
#include <optional>
#include <rock_hero/common/audio/input/audio_meter_snapshot.h>

namespace rock_hero::editor::ui
{

/*! \brief Meter fill direction used by AudioLevelMeter. */
enum class AudioLevelMeterOrientation : std::uint8_t
{
    /*! \brief Draws a left-to-right meter. */
    Horizontal,

    /*! \brief Draws a bottom-to-top meter. */
    Vertical,
};

/*! \brief Lightweight peak meter with a clipping indicator. */
class AudioLevelMeter final : public juce::Component, public juce::SettableTooltipClient
{
public:
    /*!
    \brief Creates a peak meter with the requested orientation and optional label.
    \param orientation Fill direction for the meter.
    \param label Optional label drawn inside horizontal meters.
    */
    explicit AudioLevelMeter(AudioLevelMeterOrientation orientation, juce::String label = {});

    /*!
    \brief Applies the latest meter level.
    \param level Meter level to draw.
    */
    void setLevel(common::audio::AudioMeterLevel level);

    /*!
    \brief Returns the last level applied to the meter.
    \return Current meter level.
    */
    [[nodiscard]] common::audio::AudioMeterLevel level() const noexcept;

    /*!
    \brief Marks a level the signal should reach, or clears the mark.
    \param target_db The level to mark in dBFS, or empty for no mark.
    */
    void setTargetDb(std::optional<double> target_db);

    /*!
    \brief Returns the level the meter marks.
    \return The marked level in dBFS, or empty when there is no mark.
    */
    [[nodiscard]] std::optional<double> targetDb() const noexcept;

    /*!
    \brief Paints the meter body, level fill, and clipping indicator.
    \param g Graphics context used for drawing.
    */
    void paint(juce::Graphics& g) override;

private:
    // Fill direction chosen by the owning panel.
    AudioLevelMeterOrientation m_orientation;

    // Optional compact label, currently used by the transport-bar master meter.
    juce::String m_label;

    // Most recent peak value.
    common::audio::AudioMeterLevel m_level{};

    // The level the owner wants the signal to reach, drawn as a mark across the meter.
    std::optional<double> m_target_db;

    // Clip indicator remains visible briefly so a single clipped block is noticeable.
    bool m_clip_indicator_active{false};

    // Last clip time in JUCE's millisecond counter domain.
    double m_last_clip_time_ms{0.0};
};

} // namespace rock_hero::editor::ui
