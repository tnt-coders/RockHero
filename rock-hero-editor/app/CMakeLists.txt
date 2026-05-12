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

# Register the editor as a developer-facing desktop target in the package metadata.
package_register_target(rock_hero_editor_exe DISPLAY_NAME "Rock Hero Editor")
