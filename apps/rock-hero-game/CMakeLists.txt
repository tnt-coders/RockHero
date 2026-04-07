juce_add_gui_app(rock_hero_game PRODUCT_NAME "Rock Hero" VERSION ${PROJECT_VERSION})

target_sources(rock_hero_game PRIVATE main.cpp)

# Disabled because RockHero does not use WebBrowserComponent. JUCE only propagates the required
# GTK/webkit include paths to targets that explicitly opt in via NEEDS_WEB_BROWSER TRUE.
target_compile_definitions(rock_hero_game PRIVATE JUCE_WEB_BROWSER=0)

target_link_libraries(rock_hero_game PRIVATE audio_engine song juce::juce_gui_basics
                                             juce::juce_recommended_warning_flags)

package_register_target(rock_hero_game)
