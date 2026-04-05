juce_add_gui_app(rock_hero_game PRODUCT_NAME "Rock Hero" VERSION ${PROJECT_VERSION})

target_sources(rock_hero_game PRIVATE src/main.cpp)

target_link_libraries(rock_hero_game PRIVATE audio_engine song juce::juce_gui_basics
                                             juce::juce_recommended_warning_flags)
