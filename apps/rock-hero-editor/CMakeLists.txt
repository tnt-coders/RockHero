juce_add_gui_app(rock_hero_editor PRODUCT_NAME "Rock Hero Editor" VERSION ${PROJECT_VERSION})

target_sources(rock_hero_editor PRIVATE main.cpp editor_window.cpp waveform_display.cpp)

target_include_directories(rock_hero_editor PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(
    rock_hero_editor PRIVATE audio_engine tracktion::tracktion_engine juce::juce_gui_basics
                             juce::juce_audio_utils juce::juce_recommended_warning_flags)
