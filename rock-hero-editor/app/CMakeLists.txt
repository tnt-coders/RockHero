# Editor executable startup: JUCE hosts process lifecycle and delegates behavior to product libs.
juce_add_gui_app(rock_hero_editor_exe PRODUCT_NAME "Rock Hero Editor" VERSION ${PROJECT_VERSION})

# Keep the shipped executable name stable and readable on desktop platforms.
set_target_properties(rock_hero_editor_exe PROPERTIES OUTPUT_NAME "rock-hero-editor")

# The executable owns only process startup; reusable editor behavior lives outside app/.
target_sources(rock_hero_editor_exe PRIVATE main.cpp)

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

# Deploy the 3D preview's resources next to the editor executable (plan 44): the shared highway
# shaders compiled through the same staging contract the game uses, plus the shared texture
# assets. Stamp-based for the same reason as the game deploy — POST_BUILD only runs on relink,
# which would leave stale shaders beside the executable after a shader-only edit.
rock_hero_stage_highway_shaders(rock_hero_editor_shaders
                                "${CMAKE_CURRENT_BINARY_DIR}/resources/shaders")
get_target_property(rock_hero_editor_shader_staging_dir rock_hero_editor_shaders
                    ROCK_HERO_STAGING_DIR)
get_target_property(rock_hero_editor_staged_shaders rock_hero_editor_shaders ROCK_HERO_STAGED_FILES)
set(rock_hero_editor_texture_dir "${CMAKE_SOURCE_DIR}/rock-hero-common/ui/resources/textures")
file(GLOB_RECURSE rock_hero_editor_texture_files "${rock_hero_editor_texture_dir}/*")
set(rock_hero_editor_resources_stamp "${CMAKE_CURRENT_BINARY_DIR}/resources-deployed.stamp")
add_custom_command(
    OUTPUT "${rock_hero_editor_resources_stamp}"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${rock_hero_editor_shader_staging_dir}"
            "$<TARGET_FILE_DIR:rock_hero_editor_exe>/resources/shaders"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${rock_hero_editor_texture_dir}"
            "$<TARGET_FILE_DIR:rock_hero_editor_exe>/resources/textures"
    COMMAND ${CMAKE_COMMAND} -E touch "${rock_hero_editor_resources_stamp}"
    DEPENDS ${rock_hero_editor_staged_shaders} ${rock_hero_editor_texture_files}
            rock_hero_editor_shaders
    COMMENT "Deploying editor preview resources next to the executable")
add_custom_target(rock_hero_editor_resources DEPENDS "${rock_hero_editor_resources_stamp}")
add_dependencies(rock_hero_editor_exe rock_hero_editor_resources)
