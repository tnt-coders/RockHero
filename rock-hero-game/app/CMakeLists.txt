# Main game executable: SDL owns main() and the frame loop (plan 20 gate outcome L2); JUCE runs
# as a library whose message queue the game shell drains every frame. PackageBuilder embeds the
# Windows icon resource and registers the desktop metadata that juce_add_gui_app used to provide.
package_add_executable(
    rock_hero_game_exe
    main.cpp
    DISPLAY_NAME
    "Rock Hero"
    ICON
    "${CMAKE_CURRENT_SOURCE_DIR}/resources/rock-hero.ico"
    LINUX_DESKTOP_FILE
    "${CMAKE_CURRENT_SOURCE_DIR}/resources/rock_hero.desktop"
    LINUX_ICON_THEME_DIR
    "${CMAKE_CURRENT_SOURCE_DIR}/resources/icons")

# Keep the installed executable name stable and user-facing while using a product-scope target ID.
# WIN32_EXECUTABLE gives the GUI subsystem (no console window) even though the entry point stays a
# portable main().
set_target_properties(rock_hero_game_exe PROPERTIES OUTPUT_NAME "rock-hero" WIN32_EXECUTABLE TRUE)

# The GUI subsystem defaults to a WinMain entry; point MSVC's linker at the CRT's main-based
# startup instead so the SDL_MAIN_HANDLED plain main() links.
target_link_options(rock_hero_game_exe PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/ENTRY:mainCRTStartup>)

# App-level composition may use scope umbrellas. Libraries should link narrower targets.
target_link_libraries(rock_hero_game_exe PRIVATE rock_hero::common rock_hero::game
                                                 rock_hero::build_policy)
