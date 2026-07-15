include_guard()

# RockHeroRenderStack.cmake
#
# Render-stack dependency wiring for the SDL3 + bgfx game view and the editor 3D preview, per the
# G20-RENDER gate decision (docs/plans/roadmap/20-game-architecture-and-render-stack.md § Gate record).
#
# Like the JUCE/Tracktion wrappers in RockHeroExternalModules.cmake, consumers link project-owned
# rock_hero:: aliases instead of raw third-party targets. SDL3 and bgfx arrive precompiled through
# Conan, so there is no module-source recompilation concern here; these wrappers are thin INTERFACE
# consumption seams that keep raw target names and any future render-stack compile settings in one
# place shared by rock-hero-game/ui and the future editor preview.

find_package(SDL3 REQUIRED CONFIG)
find_package(bgfx REQUIRED CONFIG)

add_library(rock_hero_sdl3 INTERFACE)
add_library(rock_hero::sdl3 ALIAS rock_hero_sdl3)
target_link_libraries(rock_hero_sdl3 INTERFACE SDL3::SDL3)

add_library(rock_hero_bgfx INTERFACE)
add_library(rock_hero::bgfx ALIAS rock_hero_bgfx)
target_link_libraries(rock_hero_bgfx INTERFACE bgfx::bgfx)

# The classic CMakeDeps generator declares no EXECUTABLE targets for the tools the bgfx package
# ships — it declares bgfx::shaderc as an interface library component, which is why the shader
# helper below cannot lean on that target name. Locate the packaged shaderc binary off the
# recipe-exported BGFX_SHADER_INCLUDE_PATH — the pattern proven by gate criterion S4 — and use
# the cached program path directly. A bgfx package-revision bump relocates the Conan package
# folder while the cached absolute path survives reconfigures, so a dangling cache entry is
# cleared before the search re-runs.
if(DEFINED ROCK_HERO_BGFX_SHADERC AND NOT EXISTS "${ROCK_HERO_BGFX_SHADERC}")
    unset(ROCK_HERO_BGFX_SHADERC CACHE)
endif()
cmake_path(GET BGFX_SHADER_INCLUDE_PATH PARENT_PATH _rock_hero_bgfx_include_root)
cmake_path(GET _rock_hero_bgfx_include_root PARENT_PATH _rock_hero_bgfx_package_root)
find_program(ROCK_HERO_BGFX_SHADERC shaderc HINTS "${_rock_hero_bgfx_package_root}/bin" REQUIRED)

# Compiles one bgfx .sc shader for one backend profile at build time (gate criterion S4,
# productionized). Callers own output layout and the profile list, so adding a backend later is a
# call-site list edit, not a redesign (plan 20 gate record, 0a memo requirement).
#
# rock_hero_add_compiled_shader(
#     OUTPUT <path to compiled .bin>
#     TYPE <vertex|fragment|compute>
#     SOURCE <path to the .sc source>
#     VARYING <path to the varying.def.sc definition>
#     PROFILE <shaderc profile, e.g. s_5_0>
#     PLATFORM <shaderc platform, e.g. windows>)
function(rock_hero_add_compiled_shader)
    set(one_value_args OUTPUT TYPE SOURCE VARYING PROFILE PLATFORM)
    cmake_parse_arguments(PARSE_ARGV 0 ARG "" "${one_value_args}" "")

    foreach(required IN LISTS one_value_args)
        if(NOT ARG_${required})
            message(FATAL_ERROR "rock_hero_add_compiled_shader: missing required argument "
                                "${required}")
        endif()
    endforeach()

    # The varying definition is a real input even though shaderc takes it as a flag, so declare
    # it as a dependency to get correct rebuilds when only the varyings change. Includes are NOT
    # tracked: today the only include dir is immutable Conan package content, so that is correct;
    # the moment project-owned shared .sh includes appear, switch to shaderc's --depends output
    # via DEPFILE (see docs/plans/todo/game-render-watch-items.md). The source rides plain DEPENDS,
    # never MAIN_DEPENDENCY: both products compile the same shared sources, and a source file may
    # be the main dependency of at most one custom command (benign under Ninja, silently drops a
    # rule under other generators).
    add_custom_command(
        OUTPUT "${ARG_OUTPUT}"
        COMMAND
            "${ROCK_HERO_BGFX_SHADERC}" -f "${ARG_SOURCE}" -o "${ARG_OUTPUT}" --type ${ARG_TYPE}
            --platform ${ARG_PLATFORM} -p ${ARG_PROFILE} -O 3 --varyingdef "${ARG_VARYING}" -i
            "${BGFX_SHADER_INCLUDE_PATH}"
        DEPENDS "${ARG_SOURCE}" "${ARG_VARYING}"
        COMMENT "Compiling ${ARG_TYPE} shader ${ARG_SOURCE} for ${ARG_PROFILE}")
endfunction()

# Stages the shared highway shader programs (rock-hero-common/ui/shaders) into
# <staging_dir>/dx11 and creates <target_name> carrying the staged tree through the
# ROCK_HERO_STAGING_DIR / ROCK_HERO_STAGED_FILES target properties (the resource-pack deploy
# contract from plan 20 Phase 2). Both products call this so the shader program list lives in
# exactly one place. Compilation is Windows-only: shaderc's HLSL backend needs the Windows D3D
# compiler; non-Windows builds are compile/test hygiene, not shipped products, so they stage an
# empty tree and the deploy still works.
#
# rock_hero_stage_highway_shaders(<target_name> <staging_dir>)
function(rock_hero_stage_highway_shaders target_name staging_dir)
    set(shader_source_dir "${CMAKE_SOURCE_DIR}/rock-hero-common/ui/shaders")
    set(staged_files "")
    if(WIN32)
        foreach(shader_program IN ITEMS color color_fade texture_tint glyph texture)
            foreach(shader_stage IN ITEMS vertex fragment)
                if(shader_stage STREQUAL "vertex")
                    set(stage_prefix vs)
                else()
                    set(stage_prefix fs)
                endif()
                rock_hero_add_compiled_shader(
                    OUTPUT
                    "${staging_dir}/dx11/${stage_prefix}_${shader_program}.bin"
                    TYPE
                    ${shader_stage}
                    SOURCE
                    "${shader_source_dir}/${stage_prefix}_${shader_program}.sc"
                    VARYING
                    "${shader_source_dir}/varying.def.sc"
                    PROFILE
                    s_5_0
                    PLATFORM
                    windows)
                list(APPEND staged_files
                     "${staging_dir}/dx11/${stage_prefix}_${shader_program}.bin")
            endforeach()
        endforeach()
    endif()

    # The staging root must exist even when nothing is staged so deploy copy_directory calls
    # have a source on every platform.
    file(MAKE_DIRECTORY "${staging_dir}")

    add_custom_target(${target_name} DEPENDS ${staged_files})

    # Custom target properties carry the staging dir and the staged-file list across directory
    # scopes: unlike a PARENT_SCOPE variable this survives subdirectory reordering, and a lookup
    # typo fails loudly (NOTFOUND) instead of silently emptying a copy command.
    set_target_properties(${target_name} PROPERTIES ROCK_HERO_STAGING_DIR "${staging_dir}"
                                                    ROCK_HERO_STAGED_FILES "${staged_files}")
endfunction()
