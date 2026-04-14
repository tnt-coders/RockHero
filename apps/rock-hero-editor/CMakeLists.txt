juce_add_gui_app(rock_hero_editor PRODUCT_NAME "Rock Hero Editor" VERSION ${PROJECT_VERSION})
set_target_properties(rock_hero_editor PROPERTIES OUTPUT_NAME "rock-hero-editor")

target_sources(rock_hero_editor PRIVATE main.cpp main_window.cpp waveform_display.cpp)

target_include_directories(rock_hero_editor PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# Disabled because RockHero does not use WebBrowserComponent or URL networking. JUCE only propagates
# the required GTK/webkit/curl libraries to targets that explicitly opt in via NEEDS_WEB_BROWSER
# TRUE or NEEDS_CURL TRUE.
target_compile_definitions(rock_hero_editor PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)

target_link_libraries(
    rock_hero_editor PRIVATE rock_hero_audio tracktion::tracktion_engine juce::juce_gui_basics
                             juce::juce_audio_utils juce::juce_recommended_warning_flags)

package_register_target(rock_hero_editor DISPLAY_NAME "Rock Hero Editor")
