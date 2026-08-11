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
# shaders compiled through the same staging contract the game uses, plus the shared texture assets.
# The editor stages its own shader tree because it builds independently of the game's ui target;
# everything after that is the shared product-resource deploy rule.
rock_hero_stage_highway_shaders(rock_hero_editor_shaders
                                "${CMAKE_CURRENT_BINARY_DIR}/resources/shaders")
rock_hero_deploy_product_resources(TARGET rock_hero_editor_exe SHADER_TARGET
                                   rock_hero_editor_shaders)
