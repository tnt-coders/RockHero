# Editor executable startup: JUCE hosts process lifecycle and delegates behavior to product libs.
juce_add_gui_app(rock_hero_editor_exe PRODUCT_NAME "Rock Hero Editor" VERSION ${PROJECT_VERSION})

# Keep the shipped executable name stable and readable on desktop platforms.
set_target_properties(rock_hero_editor_exe PROPERTIES OUTPUT_NAME "rock-hero-editor")

# The executable owns only process startup; reusable editor behavior lives outside app/.
target_sources(rock_hero_editor_exe PRIVATE main.cpp)

# Ship editor default config beside the executable for development and installed runtime layouts.
set(_rock_hero_editor_config_dir "config")
set(_rock_hero_editor_display_type_overrides
    "${CMAKE_CURRENT_SOURCE_DIR}/../core/config/plugin-display-type-overrides.json")

add_custom_command(
    TARGET rock_hero_editor_exe
    POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E make_directory
            "$<TARGET_FILE_DIR:rock_hero_editor_exe>/${_rock_hero_editor_config_dir}"
    COMMAND
        "${CMAKE_COMMAND}" -E copy_if_different "${_rock_hero_editor_display_type_overrides}"
        "$<TARGET_FILE_DIR:rock_hero_editor_exe>/${_rock_hero_editor_config_dir}/plugin-display-type-overrides.json"
    VERBATIM)

install(
    FILES "${_rock_hero_editor_display_type_overrides}"
    DESTINATION "${CMAKE_INSTALL_BINDIR}/${_rock_hero_editor_config_dir}"
    COMPONENT Runtime)

# Generate JUCE's project metadata umbrella for this app target.
rock_hero_juce_generate_juce_header(rock_hero_editor_exe)

# App-level composition may use scope umbrellas. Libraries should link narrower targets.
target_link_libraries(rock_hero_editor_exe PRIVATE rock_hero::common rock_hero::editor
                                                   rock_hero::build_policy)

if(TARGET project_config_docs_for_development)
    # Make local HTML help available when launching the editor directly from the build tree.
    add_dependencies(rock_hero_editor_exe project_config_docs_for_development)
endif()

# Register the editor as a developer-facing desktop target in the package metadata.
package_register_target(rock_hero_editor_exe DISPLAY_NAME "Rock Hero Editor")
