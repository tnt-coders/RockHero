add_library(rock_hero_audio STATIC)

target_sources(rock_hero_audio PRIVATE src/engine.cpp src/thumbnail.cpp)

target_include_directories(rock_hero_audio PUBLIC include)

# Disabled because RockHero does not use WebBrowserComponent or URL networking. JUCE only propagates
# the required GTK/webkit/curl libraries to targets that explicitly opt in via NEEDS_WEB_BROWSER
# TRUE or NEEDS_CURL TRUE.
target_compile_definitions(rock_hero_audio PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)

target_link_libraries(
    rock_hero_audio
    PUBLIC juce::juce_core
    PRIVATE tracktion::tracktion_core tracktion::tracktion_engine tracktion::tracktion_graph
            juce::juce_audio_devices juce::juce_audio_processors juce::juce_audio_utils)
