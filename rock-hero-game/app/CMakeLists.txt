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

# Deploy the resource-pack tree next to the built executable (plan 20 Phase 2): compiled shaders
# from game/ui's staging dir plus the empty asset directories later plans fill (fonts and click
# samples arrive with plan 26's art direction; textures with plan 25's atlases). The game loads
# everything under this tree through game/core's GameResources resolver.
#
# The deploy is a stamp-based build step, NOT a POST_BUILD command: under Ninja, POST_BUILD only
# runs when the executable relinks, so a shader edit would rebuild the staged .bin without
# refreshing the copy beside the executable — the dev loop would silently run stale shaders. The
# stamp depends on the staged files, so a recompiled shader re-runs the copy on the next build.
get_target_property(rock_hero_game_shader_staging_dir rock_hero_game_shaders ROCK_HERO_STAGING_DIR)
get_target_property(rock_hero_game_staged_shaders rock_hero_game_shaders ROCK_HERO_STAGED_FILES)
set(rock_hero_game_resources_stamp "${CMAKE_CURRENT_BINARY_DIR}/resources-deployed.stamp")
add_custom_command(
    OUTPUT "${rock_hero_game_resources_stamp}"
    COMMAND
        ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:rock_hero_game_exe>/resources/fonts"
        "$<TARGET_FILE_DIR:rock_hero_game_exe>/resources/sfx"
        "$<TARGET_FILE_DIR:rock_hero_game_exe>/resources/textures"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${rock_hero_game_shader_staging_dir}"
            "$<TARGET_FILE_DIR:rock_hero_game_exe>/resources/shaders"
    COMMAND ${CMAKE_COMMAND} -E touch "${rock_hero_game_resources_stamp}"
    DEPENDS ${rock_hero_game_staged_shaders} rock_hero_game_shaders
    COMMENT "Deploying game resources next to the executable")
add_custom_target(rock_hero_game_resources DEPENDS "${rock_hero_game_resources_stamp}")
add_dependencies(rock_hero_game_exe rock_hero_game_resources)

# The installed layout mirrors the build layout: resources/ beside the executable in bindir.
include(GNUInstallDirs)
install(
    DIRECTORY "${rock_hero_game_shader_staging_dir}"
    DESTINATION "${CMAKE_INSTALL_BINDIR}/resources"
    COMPONENT Runtime)
install(
    DIRECTORY
    DESTINATION "${CMAKE_INSTALL_BINDIR}/resources/fonts"
    COMPONENT Runtime)
install(
    DIRECTORY
    DESTINATION "${CMAKE_INSTALL_BINDIR}/resources/sfx"
    COMPONENT Runtime)
install(
    DIRECTORY
    DESTINATION "${CMAKE_INSTALL_BINDIR}/resources/textures"
    COMPONENT Runtime)
