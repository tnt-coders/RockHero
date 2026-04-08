juce_add_gui_app(rock_hero PRODUCT_NAME "Rock Hero" VERSION ${PROJECT_VERSION})
set_target_properties(rock_hero PROPERTIES OUTPUT_NAME "rock-hero")

target_sources(rock_hero PRIVATE main.cpp)

# Disabled because RockHero does not use WebBrowserComponent or URL networking. JUCE only propagates
# the required GTK/webkit/curl libraries to targets that explicitly opt in via NEEDS_WEB_BROWSER
# TRUE or NEEDS_CURL TRUE.
target_compile_definitions(rock_hero PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)

target_link_libraries(rock_hero PRIVATE audio_engine song juce::juce_gui_basics
                                        juce::juce_recommended_warning_flags)

package_register_target(rock_hero)
