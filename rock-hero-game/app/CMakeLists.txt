# Main game executable: JUCE bootstraps the app shell while gameplay systems grow behind it.
juce_add_gui_app(
    rock_hero_game_exe
    PRODUCT_NAME
    "Rock Hero"
    VERSION
    ${PROJECT_VERSION}
    ICON_BIG
    "${CMAKE_CURRENT_SOURCE_DIR}/resources/icons/hicolor/256x256/apps/rock-hero.png"
    ICON_SMALL
    "${CMAKE_CURRENT_SOURCE_DIR}/resources/icons/hicolor/64x64/apps/rock-hero.png")

# Keep the installed executable name stable and user-facing while using a product-scope target ID.
set_target_properties(rock_hero_game_exe PROPERTIES OUTPUT_NAME "rock-hero")

# The current app remains intentionally small: startup lives in one translation unit.
target_sources(rock_hero_game_exe PRIVATE main.cpp)

# Generate JUCE's project metadata umbrella for this app target.
rock_hero_juce_generate_juce_header(rock_hero_game_exe)

# App-level composition may use scope umbrellas. Libraries should link narrower targets.
target_link_libraries(rock_hero_game_exe PRIVATE rock_hero::common rock_hero::game
                                                 rock_hero::juce_gui_basics rock_hero::build_policy)

# Register the packaged desktop app metadata and icons.
package_register_target(
    rock_hero_game_exe
    DISPLAY_NAME
    "Rock Hero"
    ICON
    "${CMAKE_CURRENT_SOURCE_DIR}/resources/rock-hero.ico"
    LINUX_DESKTOP_FILE
    "${CMAKE_CURRENT_SOURCE_DIR}/resources/rock_hero.desktop"
    LINUX_ICON_THEME_DIR
    "${CMAKE_CURRENT_SOURCE_DIR}/resources/icons")
