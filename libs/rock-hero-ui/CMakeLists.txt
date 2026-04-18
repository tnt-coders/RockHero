project_config_juce_add_binary_data(rock_hero_ui_resources SOURCES resources/play_arrow.svg
                                    resources/pause.svg resources/stop.svg)

add_library(rock_hero_ui STATIC)

target_sources(rock_hero_ui PRIVATE src/waveform_display.cpp src/transport_controls.cpp)

target_include_directories(
    rock_hero_ui
    PUBLIC include
    PRIVATE include/rock_hero/ui)

# Disabled because RockHero does not use WebBrowserComponent or URL networking. JUCE only propagates
# the required GTK/webkit/curl libraries to targets that explicitly opt in via NEEDS_WEB_BROWSER
# TRUE or NEEDS_CURL TRUE.
target_compile_definitions(rock_hero_ui PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)

target_link_libraries(
    rock_hero_ui
    PUBLIC juce::juce_gui_basics
    PRIVATE rock_hero_audio rock_hero_ui_resources)
