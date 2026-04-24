juce_add_gui_app(rock_hero_editor PRODUCT_NAME "Rock Hero Editor" VERSION ${PROJECT_VERSION})
set_target_properties(rock_hero_editor PROPERTIES OUTPUT_NAME "rock-hero-editor")

target_sources(rock_hero_editor PRIVATE main.cpp main_window.cpp)

target_include_directories(rock_hero_editor PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

rock_hero_juce_generate_juce_header(rock_hero_editor)

target_link_libraries(
    rock_hero_editor PRIVATE rock_hero_ui rock_hero_audio rock_hero_juce_gui_basics
                             rock_hero_juce_audio_utils juce::juce_recommended_warning_flags)

package_register_target(rock_hero_editor DISPLAY_NAME "Rock Hero Editor")
