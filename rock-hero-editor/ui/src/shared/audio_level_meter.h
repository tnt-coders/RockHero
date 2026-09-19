/*!
\file audio_level_meter.h
\brief Compact JUCE peak meter used by editor audio panels.
*/

#pragma once

#include <cstdint>
#include <juce_gui_basics/juce_gui_basics.h>
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

/*!
\brief Frame edge an AudioLevelMeter leaves undrawn.

A meter set flush against another surface shares that border with it, and drawing it reads as a wall
between the two. Leaving the shared edge open lets whatever the meter abuts carry straight through.
*/
enum class AudioLevelMeterOpenEdge : std::uint8_t
{
    /*! \brief Draws the full frame; the default for a meter that stands on its own. */
    None,

    /*! \brief Leaves the left border undrawn. */
    Left,

    /*! \brief Leaves the right border undrawn. */
    Right,
};

/*! \brief Lightweight peak meter with a clipping indicator. */
class AudioLevelMeter final : public juce::Component
{
public:
    /*!
    \brief Creates a peak meter with the requested orientation and optional label.
    \param orientation Fill direction for the meter.
    \param label Optional label drawn inside horizontal meters.
    \param open_edge Frame edge to leave undrawn where the meter sits flush against another surface.
    */
    explicit AudioLevelMeter(
        AudioLevelMeterOrientation orientation, juce::String label = {},
        AudioLevelMeterOpenEdge open_edge = AudioLevelMeterOpenEdge::None);

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
    \brief Paints the meter body, level fill, and clipping indicator.
    \param g Graphics context used for drawing.
    */
    void paint(juce::Graphics& g) override;

private:
    // Fill direction chosen by the owning panel.
    AudioLevelMeterOrientation m_orientation;

    // Optional compact label, currently used by the transport-bar master meter.
    juce::String m_label;

    // Frame edge left undrawn so an abutting surface reads as continuing through the meter.
    AudioLevelMeterOpenEdge m_open_edge;

    // Most recent peak value.
    common::audio::AudioMeterLevel m_level{};

    // Clip indicator remains visible briefly so a single clipped block is noticeable.
    bool m_clip_indicator_active{false};

    // Last clip time in JUCE's millisecond counter domain.
    double m_last_clip_time_ms{0.0};
};

} // namespace rock_hero::editor::ui
