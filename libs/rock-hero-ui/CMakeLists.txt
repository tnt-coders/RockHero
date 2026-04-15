juce_add_binary_data(rock_hero_ui_resources SOURCES resources/play_arrow.svg resources/pause.svg
                     resources/stop.svg)

# BinaryData.h is generated at build time by juce_add_binary_data (via juceaide). The clang-tidy and
# clang-tidy-fix targets read compile_commands.json without building, so the header would not exist
# when transport_controls.cpp is analyzed. These dependencies ensure juceaide runs first — it only
# converts the SVG files to C++ and takes a few seconds; it does not compile JUCE or Tracktion.
foreach(tidy_target IN ITEMS clang-tidy clang-tidy-fix)
    if(TARGET ${tidy_target})
        add_dependencies(${tidy_target} rock_hero_ui_resources)
    endif()
endforeach()

add_library(rock_hero_ui STATIC)

target_sources(rock_hero_ui PRIVATE src/waveform_display.cpp src/transport_controls.cpp)

target_include_directories(rock_hero_ui PUBLIC include)

# Disabled because RockHero does not use WebBrowserComponent or URL networking. JUCE only propagates
# the required GTK/webkit/curl libraries to targets that explicitly opt in via NEEDS_WEB_BROWSER
# TRUE or NEEDS_CURL TRUE.
target_compile_definitions(rock_hero_ui PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)

target_link_libraries(
    rock_hero_ui
    PUBLIC juce::juce_gui_basics
    PRIVATE rock_hero_audio rock_hero_ui_resources tracktion::tracktion_core
            tracktion::tracktion_engine juce::juce_audio_utils)
