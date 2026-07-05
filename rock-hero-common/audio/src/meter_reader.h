/*!
\file meter_reader.h
\brief On-demand reader for Tracktion level-measurer clients used by the engine's meter port.
*/

#pragma once

#include <rock_hero/common/audio/input/audio_meter_snapshot.h>
#include <tracktion_engine/tracktion_engine.h>

namespace rock_hero::common::audio
{

/*!
\brief Owns one Tracktion meter client and converts its peak window into a project value.
*/
class MeterReader
{
public:
    /*! \brief Creates a detached reader. */
    MeterReader() = default;

    /*! \brief Copying is disabled; the client registration is identity-based. */
    MeterReader(const MeterReader&) = delete;

    /*! \brief Copy assignment is disabled; the client registration is identity-based. */
    MeterReader& operator=(const MeterReader&) = delete;

    /*! \brief Moving is disabled; Tracktion holds a pointer to the client member. */
    MeterReader(MeterReader&&) = delete;

    /*! \brief Move assignment is disabled; Tracktion points at the client member. */
    MeterReader& operator=(MeterReader&&) = delete;

    /*! \brief Detaches from Tracktion before the reader's client storage is destroyed. */
    ~MeterReader()
    {
        detach();
    }

    /*!
    \brief Moves this reader to a new Tracktion measurer after graph rebuilds.
    \param measurer Measurer to observe, or null to only detach.
    */
    void attach(tracktion::LevelMeasurer* measurer)
    {
        if (m_measurer == measurer)
        {
            return;
        }

        detach();
        if (measurer == nullptr)
        {
            return;
        }

        measurer->addClient(m_client);
        m_measurer = measurer;
    }

    /*! \brief Removes the client from the current measurer, if one is attached. */
    void detach()
    {
        if (m_measurer != nullptr)
        {
            m_measurer->removeClient(m_client);
            m_measurer = nullptr;
        }
        m_client.reset();
    }

    /*!
    \brief Reads and clears the peak window accumulated since the last snapshot.
    \return Peak level for the window, clamped to the project meter range.
    */
    [[nodiscard]] AudioMeterLevel read()
    {
        if (m_measurer == nullptr)
        {
            return {};
        }

        constexpr int max_channels = tracktion::LevelMeasurer::Client::maxNumChannels;
        const int channel_count = std::clamp(m_client.getNumChannelsUsed(), 0, max_channels);
        double peak_db = minimumAudioMeterDb();
        for (int channel = 0; channel < channel_count; ++channel)
        {
            const tracktion::DbTimePair level = m_client.getAndClearAudioLevel(channel);
            if (std::isfinite(level.dB))
            {
                peak_db = std::max(peak_db, static_cast<double>(level.dB));
            }
        }

        return AudioMeterLevel{
            .peak_db = std::clamp(peak_db, minimumAudioMeterDb(), 12.0),
            .clipping = peak_db >= clippingAudioMeterDb(),
        };
    }

private:
    // Tracktion-owned measurer currently feeding this reader.
    tracktion::LevelMeasurer* m_measurer{};

    // Client object registered with the Tracktion measurer while attached.
    tracktion::LevelMeasurer::Client m_client;
};

} // namespace rock_hero::common::audio
