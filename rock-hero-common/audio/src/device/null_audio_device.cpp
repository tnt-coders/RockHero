#include "device/null_audio_device.h"

#include <algorithm>
#include <utility>

namespace rock_hero::common::audio
{

namespace
{

constexpr const char* g_null_audio_device_type_name = "Silent Device";

// One format, so the manager's best-rate and best-size choices always land on it: plugins and the
// graph run at a common studio rate, at a block the playhead extrapolator smooths easily.
constexpr double g_null_sample_rate_hz{48000.0};
constexpr int g_null_block_size{512};
constexpr int g_null_output_channel_count{2};

// The silent device: output-only, and clocked by its own thread, because Tracktion's graph only
// advances on a device callback. The thread renders into a buffer it never plays and paces
// itself to wall-clock time, so the playhead, the plugins and the automation run exactly as on
// hardware. It takes the audio thread's role and its rules: nothing in the loop allocates or locks.
class NullAudioIODevice final : public juce::AudioIODevice, private juce::Thread
{
public:
    NullAudioIODevice()
        : juce::AudioIODevice(g_null_audio_device_name, g_null_audio_device_type_name)
        , juce::Thread("Silent audio device")
    {}

    ~NullAudioIODevice() override
    {
        close();
    }

    NullAudioIODevice(const NullAudioIODevice&) = delete;
    NullAudioIODevice& operator=(const NullAudioIODevice&) = delete;
    NullAudioIODevice(NullAudioIODevice&&) = delete;
    NullAudioIODevice& operator=(NullAudioIODevice&&) = delete;

    [[nodiscard]] juce::StringArray getOutputChannelNames() override
    {
        return {"Left", "Right"};
    }

    [[nodiscard]] juce::StringArray getInputChannelNames() override
    {
        return {};
    }

    [[nodiscard]] juce::Array<double> getAvailableSampleRates() override
    {
        return {g_null_sample_rate_hz};
    }

    [[nodiscard]] juce::Array<int> getAvailableBufferSizes() override
    {
        return {g_null_block_size};
    }

    [[nodiscard]] int getDefaultBufferSize() override
    {
        return g_null_block_size;
    }

    [[nodiscard]] juce::String open(
        const juce::BigInteger& /*input_channels*/, const juce::BigInteger& output_channels,
        double /*sample_rate*/, int /*buffer_size_samples*/) override
    {
        close();
        m_active_outputs = output_channels.getBitRange(0, g_null_output_channel_count);
        // Sized here, off the audio thread, so the render loop never allocates.
        m_output.setSize(std::max(1, m_active_outputs.countNumberOfSetBits()), g_null_block_size);
        m_open = true;
        return {};
    }

    void close() override
    {
        stop();
        m_open = false;
    }

    [[nodiscard]] bool isOpen() override
    {
        return m_open;
    }

    void start(juce::AudioIODeviceCallback* callback) override
    {
        if (!m_open || callback == nullptr)
        {
            return;
        }

        stop();
        callback->audioDeviceAboutToStart(this);
        // Published before the thread starts and cleared after it joins, so the render loop reads
        // it without a lock.
        m_callback = callback;
        static_cast<void>(startRealtimeThread(
            juce::Thread::RealtimeOptions{}.withApproximateAudioProcessingTime(
                g_null_block_size, g_null_sample_rate_hz)));
    }

    void stop() override
    {
        if (m_callback == nullptr)
        {
            return;
        }

        // The loop waits on the thread's own event, which signalling the exit wakes, so the join
        // takes at most one block's render.
        signalThreadShouldExit();
        static_cast<void>(stopThread(-1));
        juce::AudioIODeviceCallback* const callback = std::exchange(m_callback, nullptr);
        callback->audioDeviceStopped();
    }

    [[nodiscard]] bool isPlaying() override
    {
        return m_callback != nullptr;
    }

    [[nodiscard]] juce::String getLastError() override
    {
        return {};
    }

    [[nodiscard]] int getCurrentBufferSizeSamples() override
    {
        return g_null_block_size;
    }

    [[nodiscard]] double getCurrentSampleRate() override
    {
        return m_open ? g_null_sample_rate_hz : 0.0;
    }

    [[nodiscard]] int getCurrentBitDepth() override
    {
        return 32;
    }

    [[nodiscard]] juce::BigInteger getActiveOutputChannels() const override
    {
        return m_active_outputs;
    }

    [[nodiscard]] juce::BigInteger getActiveInputChannels() const override
    {
        return {};
    }

    [[nodiscard]] int getOutputLatencyInSamples() override
    {
        return 0;
    }

    [[nodiscard]] int getInputLatencyInSamples() override
    {
        return 0;
    }

private:
    void run() override
    {
        NullAudioDeviceClock pacing{
            .start_ms = juce::Time::getMillisecondCounterHiRes(),
            .blocks = 0,
            .period_ms = 1000.0 * g_null_block_size / g_null_sample_rate_hz,
        };
        while (!threadShouldExit())
        {
            m_output.clear();
            m_callback->audioDeviceIOCallbackWithContext(
                nullptr,
                0,
                m_output.getArrayOfWritePointers(),
                m_output.getNumChannels(),
                m_output.getNumSamples(),
                {});
            if (const double wait_ms = pacing.advance(juce::Time::getMillisecondCounterHiRes());
                wait_ms > 0.0)
            {
                static_cast<void>(wait(wait_ms));
            }
        }
    }

    bool m_open{false};
    juce::BigInteger m_active_outputs;
    juce::AudioBuffer<float> m_output;
    juce::AudioIODeviceCallback* m_callback{nullptr};
};

// Offers the one silent device. Its list never changes, so JUCE's disconnect handling never evicts
// it, and it has no inputs, so live input is unavailable on it by construction.
class NullAudioIODeviceType final : public juce::AudioIODeviceType
{
public:
    NullAudioIODeviceType()
        : juce::AudioIODeviceType(g_null_audio_device_type_name)
    {}

    void scanForDevices() override
    {}

    [[nodiscard]] juce::StringArray getDeviceNames(bool want_input_names) const override
    {
        return want_input_names ? juce::StringArray{} : juce::StringArray{g_null_audio_device_name};
    }

    [[nodiscard]] int getDefaultDeviceIndex(bool /*for_input*/) const override
    {
        return 0;
    }

    [[nodiscard]] int getIndexOfDevice(juce::AudioIODevice* device, bool as_input) const override
    {
        return !as_input && isNullAudioDevice(device) ? 0 : -1;
    }

    [[nodiscard]] bool hasSeparateInputsAndOutputs() const override
    {
        return true;
    }

    [[nodiscard]] juce::AudioIODevice* createDevice(
        const juce::String& output_device_name, const juce::String& /*input_device_name*/) override
    {
        if (output_device_name != g_null_audio_device_name)
        {
            return nullptr;
        }

        return std::make_unique<NullAudioIODevice>().release();
    }
};

} // namespace

// Rationale lives on the declaration in null_audio_device.h.
double NullAudioDeviceClock::advance(double now_ms) noexcept
{
    ++blocks;
    const double due_ms = start_ms + (static_cast<double>(blocks) * period_ms);
    if (now_ms - due_ms > period_ms)
    {
        start_ms = now_ms;
        blocks = 0;
        return 0.0;
    }

    return std::max(0.0, due_ms - now_ms);
}

std::unique_ptr<juce::AudioIODeviceType> createNullAudioDeviceType()
{
    return std::make_unique<NullAudioIODeviceType>();
}

bool isNullAudioDevice(const juce::AudioIODevice* device) noexcept
{
    return device != nullptr && device->getTypeName() == g_null_audio_device_type_name;
}

bool isNullAudioDeviceType(const juce::AudioIODeviceType& type) noexcept
{
    return type.getTypeName() == g_null_audio_device_type_name;
}

bool hardwareDeviceOpen(juce::AudioDeviceManager& device_manager)
{
    // Not a pointer to const: JUCE's device getters are not const-qualified.
    juce::AudioIODevice* const device = device_manager.getCurrentAudioDevice();
    return device != nullptr && !isNullAudioDevice(device) && device->isOpen() &&
           device->getCurrentSampleRate() > 0.0;
}

void openNullAudioDevice(juce::AudioDeviceManager& device_manager)
{
    const auto& types = device_manager.getAvailableDeviceTypes();
    const bool registered = std::ranges::any_of(
        types, [](const juce::AudioIODeviceType* type) { return isNullAudioDeviceType(*type); });
    if (!registered)
    {
        // Without the silent type JUCE's search by name would open some OTHER default device,
        // which is exactly the audible fallback this must never be.
        jassertfalse;
        return;
    }

    static_cast<void>(device_manager.initialise(1, 2, nullptr, false, g_null_audio_device_name));
}

juce::String openRouteWithoutFallback(
    juce::AudioDeviceManager& device_manager, const juce::XmlElement& route)
{
    return device_manager.initialise(1, 2, &route, false, g_null_audio_device_name);
}

} // namespace rock_hero::common::audio
