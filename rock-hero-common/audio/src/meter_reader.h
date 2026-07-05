/*!
\file meter_reader.h
\brief On-demand reader for Tracktion level-measurer clients used by the engine's meter port.
*/

#pragma once

#include <rock_hero/common/audio/input/audio_meter_snapshot.h>
#include <tracktion_engine/tracktion_engine.h>

namespace rock_hero::common::audio
{

// Owns one Tracktion meter client and converts its most recent peak window into a project value.
class MeterReader
{
public:
    MeterReader() = default;
    MeterReader(const MeterReader&) = delete;
    MeterReader& operator=(const MeterReader&) = delete;
    MeterReader(MeterReader&&) = delete;
    MeterReader& operator=(MeterReader&&) = delete;

    // Detaches from Tracktion before the reader's client storage is destroyed.
    ~MeterReader()
    {
        detach();
    }

    // Moves this reader to a new Tracktion measurer when playback graphs or plugins are rebuilt.
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

    // Removes the client from the current Tracktion measurer, if one is still attached.
    void detach()
    {
        if (m_measurer != nullptr)
        {
            m_measurer->removeClient(m_client);
            m_measurer = nullptr;
        }
        m_client.reset();
    }

    // Reads and clears the peak window accumulated since the last snapshot.
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
