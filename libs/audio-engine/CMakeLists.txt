add_library(audio_engine STATIC)

target_sources(audio_engine PRIVATE src/audio_engine.cpp)

target_include_directories(audio_engine PUBLIC include)

# Disabled because RockHero does not use WebBrowserComponent. JUCE only propagates the required
# GTK/webkit include paths to targets that explicitly opt in via NEEDS_WEB_BROWSER TRUE.
target_compile_definitions(audio_engine PRIVATE JUCE_WEB_BROWSER=0)

target_link_libraries(
    audio_engine
    PUBLIC juce::juce_core
    PRIVATE tracktion::tracktion_core tracktion::tracktion_engine tracktion::tracktion_graph
            juce::juce_audio_devices juce::juce_audio_processors juce::juce_audio_utils)
