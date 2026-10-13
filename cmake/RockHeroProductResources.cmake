include_guard()

# RockHeroProductResources.cmake
#
# The one product resource-pack deploy rule, shared by the game executable and the editor
# executable, whose 3D preview renders the same highway. Both products ship the same tree beside
# their executable — compiled highway shaders staged by rock_hero_stage_highway_shaders, plus the
# built-in highway themes and the highway's fixed art committed under rock-hero-common/ui — so the
# copy commands, the stamp contract, and the install layout live here instead of being restated per
# product, where they had already drifted apart.

include(GNUInstallDirs)

# Deploys one product's resource pack next to its executable and installs the same layout.
#
# The deploy is a stamp-based build step, NOT a POST_BUILD command: under Ninja, POST_BUILD only
# runs when the executable relinks, so a shader edit would rebuild the staged .bin without
# refreshing the copy beside the executable — the dev loop would silently run stale shaders. The
# stamp depends on the staged files and on the theme and fixed-art sources, so a recompiled shader
# or an edited theme re-runs the copy on the next build.
#
# rock_hero_deploy_product_resources(
#     TARGET <executable target>
#     SHADER_TARGET <staging target created by rock_hero_stage_highway_shaders>
#     [EMPTY_DIRS <name>...])
#
# EMPTY_DIRS names resource subdirectories that ship empty for now (the game's fonts and sfx trees,
# which later plans fill); they are created beside the executable and installed as empty directories
# so the resolver finds them either way.
function(rock_hero_deploy_product_resources)
    cmake_parse_arguments(PARSE_ARGV 0 ARG "" "TARGET;SHADER_TARGET" "EMPTY_DIRS")

    if(ARG_UNPARSED_ARGUMENTS)
        message(FATAL_ERROR "rock_hero_deploy_product_resources: unknown arguments: "
                            "${ARG_UNPARSED_ARGUMENTS}")
    endif()

    foreach(required IN ITEMS TARGET SHADER_TARGET)
        if(NOT ARG_${required})
            message(FATAL_ERROR "rock_hero_deploy_product_resources: missing required argument "
                                "${required}")
        endif()
    endforeach()

    get_target_property(shader_staging_dir ${ARG_SHADER_TARGET} ROCK_HERO_STAGING_DIR)
    get_target_property(staged_shaders ${ARG_SHADER_TARGET} ROCK_HERO_STAGED_FILES)

    # The built-in themes are owned by rock-hero-common/ui and read back from the target property
    # it sets (shared asset sources live under common and deploy per product). Reading the path
    # rather than spelling it here is what makes moving that directory a configure-time failure
    # instead of a deploy that silently copies nothing.
    get_target_property(themes_dir rock_hero_common_ui ROCK_HERO_THEMES_DIR)
    if(NOT themes_dir OR NOT IS_DIRECTORY "${themes_dir}")
        message(
            FATAL_ERROR
                "rock_hero_deploy_product_resources: rock_hero_common_ui does not report an "
                "existing ROCK_HERO_THEMES_DIR (got '${themes_dir}')")
    endif()

    # The glob feeds the stamp's dependency list so a theme edit re-runs the copy; adding a file
    # still needs a reconfigure, which theme drops rarely do without one. No theme.json means the
    # themes moved out from under this rule, which must fail here rather than deploy a tree the
    # products refuse at startup.
    file(GLOB_RECURSE theme_files CONFIGURE_DEPENDS "${themes_dir}/*")
    file(GLOB theme_manifests "${themes_dir}/*/theme.json")
    if(NOT theme_manifests)
        message(FATAL_ERROR "rock_hero_deploy_product_resources: no built-in theme found under "
                            "'${themes_dir}'")
    endif()

    # The highway's fixed art (the technique-mark sheet the renderer composes under every theme's
    # heads) is read back the same way, and fails the same way when it has moved.
    get_target_property(highway_art_dir rock_hero_common_ui ROCK_HERO_HIGHWAY_ART_DIR)
    if(NOT highway_art_dir OR NOT IS_DIRECTORY "${highway_art_dir}")
        message(
            FATAL_ERROR
                "rock_hero_deploy_product_resources: rock_hero_common_ui does not report an "
                "existing ROCK_HERO_HIGHWAY_ART_DIR (got '${highway_art_dir}')")
    endif()
    file(GLOB_RECURSE highway_art_files CONFIGURE_DEPENDS "${highway_art_dir}/*")

    set(resources_root "$<TARGET_FILE_DIR:${ARG_TARGET}>/resources")
    set(empty_dir_paths "")
    foreach(empty_dir IN LISTS ARG_EMPTY_DIRS)
        list(APPEND empty_dir_paths "${resources_root}/${empty_dir}")
    endforeach()

    set(make_empty_dirs_command "")
    if(empty_dir_paths)
        set(make_empty_dirs_command COMMAND ${CMAKE_COMMAND} -E make_directory ${empty_dir_paths})
    endif()

    set(stamp "${CMAKE_CURRENT_BINARY_DIR}/resources-deployed.stamp")
    # Each deployed tree is removed before it is copied, so it mirrors its source exactly: a copy
    # alone is additive, and a theme or shader the source dropped would linger beside the
    # executable — a lingering theme that inherits a renamed base fails the whole built-in set at
    # startup. The source directories are dependencies too, so removing a theme (which changes its
    # parent's timestamp and no file the glob still lists) re-runs the deploy.
    #
    # make_empty_dirs_command LOOKS like part of the OUTPUT list (cmake-format folds it there) but
    # is not one: it expands to nothing, or to a leading `COMMAND ...` that terminates the OUTPUT
    # arguments, so the stamp stays the sole output either way.
    add_custom_command(
        OUTPUT "${stamp}" ${make_empty_dirs_command}
        COMMAND ${CMAKE_COMMAND} -E rm -rf "${resources_root}/shaders" "${resources_root}/themes"
                "${resources_root}/highway"
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${shader_staging_dir}"
                "${resources_root}/shaders"
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${themes_dir}" "${resources_root}/themes"
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${highway_art_dir}" "${resources_root}/highway"
        COMMAND ${CMAKE_COMMAND} -E touch "${stamp}"
        DEPENDS ${staged_shaders} ${theme_files} ${highway_art_files} "${themes_dir}"
                "${highway_art_dir}" ${ARG_SHADER_TARGET}
        COMMENT "Deploying ${ARG_TARGET} resources next to the executable")
    add_custom_target(${ARG_TARGET}_resources DEPENDS "${stamp}")
    add_dependencies(${ARG_TARGET} ${ARG_TARGET}_resources)

    # The installed layout mirrors the build layout: resources/ beside the executable in bindir.
    # Both products install their own copy so each stays self-contained if they are ever packaged
    # separately or their program lists diverge.
    install(
        DIRECTORY "${shader_staging_dir}"
        DESTINATION "${CMAKE_INSTALL_BINDIR}/resources"
        COMPONENT Runtime)
    install(
        DIRECTORY "${themes_dir}/"
        DESTINATION "${CMAKE_INSTALL_BINDIR}/resources/themes"
        COMPONENT Runtime)
    install(
        DIRECTORY "${highway_art_dir}/"
        DESTINATION "${CMAKE_INSTALL_BINDIR}/resources/highway"
        COMPONENT Runtime)
    foreach(empty_dir IN LISTS ARG_EMPTY_DIRS)
        install(
            DIRECTORY
            DESTINATION "${CMAKE_INSTALL_BINDIR}/resources/${empty_dir}"
            COMPONENT Runtime)
    endforeach()
endfunction()
