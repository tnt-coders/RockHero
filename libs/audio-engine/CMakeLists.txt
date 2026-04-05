add_library(audio_engine STATIC)

target_sources(audio_engine PRIVATE src/AudioEngine.cpp)

target_include_directories(audio_engine PUBLIC include)

target_link_libraries(
    audio_engine
    PUBLIC juce::juce_core
    PRIVATE tracktion::tracktion_core tracktion::tracktion_engine tracktion::tracktion_graph
            juce::juce_audio_devices juce::juce_audio_processors juce::juce_audio_utils)
