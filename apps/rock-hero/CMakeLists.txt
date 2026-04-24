juce_add_gui_app(
    rock_hero
    PRODUCT_NAME
    "Rock Hero"
    VERSION
    ${PROJECT_VERSION}
    ICON_BIG
    "${CMAKE_CURRENT_SOURCE_DIR}/resources/icons/hicolor/256x256/apps/rock-hero.png"
    ICON_SMALL
    "${CMAKE_CURRENT_SOURCE_DIR}/resources/icons/hicolor/64x64/apps/rock-hero.png")
set_target_properties(rock_hero PROPERTIES OUTPUT_NAME "rock-hero")

target_sources(rock_hero PRIVATE main.cpp)

rock_hero_juce_generate_juce_header(rock_hero)

target_link_libraries(
    rock_hero PRIVATE rock_hero_ui rock_hero_audio rock_hero_core rock_hero_juce_gui_basics
                      juce::juce_recommended_warning_flags)

package_register_target(
    rock_hero
    DISPLAY_NAME
    "Rock Hero"
    ICON
    "${CMAKE_CURRENT_SOURCE_DIR}/resources/rock-hero.ico"
    LINUX_DESKTOP_FILE
    "${CMAKE_CURRENT_SOURCE_DIR}/resources/rock_hero.desktop"
    LINUX_ICON_THEME_DIR
    "${CMAKE_CURRENT_SOURCE_DIR}/resources/icons")
