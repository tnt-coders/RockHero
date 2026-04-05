juce_add_gui_app(rock_hero_editor PRODUCT_NAME "Rock Hero Editor" VERSION ${PROJECT_VERSION})

target_sources(rock_hero_editor PRIVATE src/main.cpp src/EditorWindow.cpp src/WaveformDisplay.cpp)

target_include_directories(rock_hero_editor PRIVATE include)

target_link_libraries(
    rock_hero_editor PRIVATE audio_engine tracktion::tracktion_engine juce::juce_gui_basics
                             juce::juce_audio_utils juce::juce_recommended_warning_flags)
