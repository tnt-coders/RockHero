include_guard()

# RockHeroExternalModules.cmake
#
# This file exists because JUCE and Tracktion are not shipped to CMake consumers as ordinary
# precompiled libraries. Their `juce_add_module`/`juce_add_modules` targets are interface-style
# module bundles that propagate include paths, compile definitions, and module sources through the
# target graph. In a modular repository like Rock Hero, linking those raw module targets directly
# from multiple first-party libraries and apps causes the same third-party module sources to be
# compiled repeatedly in multiple targets.
#
# We deliberately convert the subset of JUCE and Tracktion modules used by this repository into
# project-owned static wrapper targets. Each wrapper links the upstream module target privately and
# forwards the usage requirements that consumers still need. The rest of the project then links the
# wrapper targets instead of the raw third-party modules.
#
# Why we chose this approach: - Rock Hero is intentionally split into multiple static libraries and
# app targets. - Recompiling JUCE/Tracktion module sources independently in each of those targets is
# wasteful. - Propagating raw module targets publicly makes third-party build behavior contagious in
# places that should only be consuming first-party libraries.
#
# Tradeoffs: - The dependency lists below are now project-owned and must be kept in sync with
# upstream module declarations when JUCE or Tracktion is updated. - This is more custom than the
# default JUCE/Tracktion recommendation of linking raw modules privately on each consuming target. -
# If upstream changes its module graph or target metadata, this file may need maintenance.
#
# We accept those tradeoffs because the resulting build graph behaves much more like a normal
# modular C++ project: Rock Hero targets link stable first-party wrapper libraries, while the raw
# third-party module graph stays contained in one place.

# Place the wrapper anchor in the build tree so the repository does not need to carry a checked-in
# dummy source file just to satisfy static-library generators.
set(ROCK_HERO_EXTERNAL_MODULE_ANCHOR
    "${CMAKE_CURRENT_BINARY_DIR}/rock_hero_external_module_anchor.cpp")

# Generate the shared anchor source once during configure/generate. Every wrapper target can reuse
# this file because it contains no target-specific code; it only gives the wrapper a concrete source
# so the target behaves like a normal static library instead of an empty archive edge case.
file(
    GENERATE
    OUTPUT "${ROCK_HERO_EXTERNAL_MODULE_ANCHOR}"
    CONTENT "// Generated anchor for Rock Hero external-module wrapper targets.\n")

# Creates a normal static wrapper around an interface-style third-party module target.
#
# The wrapper's anchor source gives CMake a concrete compilation unit so the target behaves like a
# normal static library in the build graph, while the private upstream module link contributes the
# actual third-party module sources and usage requirements.
function(rock_hero_add_external_module_wrapper target upstream_target)
    set(multi_value_args PUBLIC_DEPS)
    cmake_parse_arguments(ARG "" "" "${multi_value_args}" ${ARGN})

    add_library(${target} STATIC "${ROCK_HERO_EXTERNAL_MODULE_ANCHOR}")

    # Link the upstream interface-style module privately so this wrapper target, not every consumer,
    # owns the third-party module source compilation. Re-export any first-party wrapper dependencies
    # publicly so consumers see the wrapped module graph rather than the raw third-party targets.
    target_link_libraries(
        ${target}
        PRIVATE ${upstream_target}
        PUBLIC ${ARG_PUBLIC_DEPS})

    # Re-export this target's compile definitions so consumers compile with the same
    # module-availability and feature-view that the wrapper itself was built with.
    target_compile_definitions(${target} INTERFACE $<TARGET_PROPERTY:${target},COMPILE_DEFINITIONS>)

    # Re-export the include directories gathered from the wrapped third-party module so consumers
    # can compile public headers that depend on the wrapped module's headers.
    target_include_directories(${target} INTERFACE $<TARGET_PROPERTY:${target},INCLUDE_DIRECTORIES>)

    # Re-export compile options from the wrapped module target so consumers see the same required
    # compile-time behavior as the wrapper.
    target_compile_options(${target} INTERFACE $<TARGET_PROPERTY:${target},COMPILE_OPTIONS>)

    # Re-export link options from the wrapped module target so consumers inherit any required
    # linker-side behavior without linking the raw module directly.
    target_link_options(${target} INTERFACE $<TARGET_PROPERTY:${target},LINK_OPTIONS>)
endfunction()

# JUCE wrappers need the shared curl/web opt-out exported because those values must remain
# consistent anywhere the corresponding JUCE headers are compiled.
function(rock_hero_add_external_juce_module_wrapper target juce_target)
    set(multi_value_args PUBLIC_DEPS)
    cmake_parse_arguments(ARG "" "" "${multi_value_args}" ${ARGN})

    rock_hero_add_external_module_wrapper(${target} ${juce_target} PUBLIC_DEPS ${ARG_PUBLIC_DEPS})

    # Export the JUCE feature toggles that must stay consistent anywhere these JUCE headers are
    # compiled. This keeps consumers aligned with the wrapper's JUCE feature configuration.
    target_compile_definitions(${target} PUBLIC JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)

    # Apply JUCE's recommended warnings, optimisation/debug settings, and Release LTO policy to all
    # project-owned JUCE wrapper targets so every consumer of the wrapper layer inherits one
    # consistent JUCE build policy.
    target_link_libraries(
        ${target} INTERFACE juce::juce_recommended_warning_flags
                            juce::juce_recommended_config_flags juce::juce_recommended_lto_flags)
endfunction()

# JUCE module declaration and docs:
# https://github.com/juce-framework/JUCE/blob/master/modules/juce_core/juce_core.h
# https://docs.juce.com/master/group__juce__core.html Declared deps: none.
rock_hero_add_external_juce_module_wrapper(rock_hero_juce_core juce::juce_core)

# JUCE module declaration and docs:
# https://github.com/juce-framework/JUCE/blob/master/modules/juce_events/juce_events.h
# https://docs.juce.com/master/group__juce__events.html Declared deps: juce_core.
rock_hero_add_external_juce_module_wrapper(rock_hero_juce_events juce::juce_events PUBLIC_DEPS
                                           rock_hero_juce_core)

# JUCE module declaration and docs: Source header:
# external/tracktion_engine/modules/juce_data_structures/juce_data_structures.h
# https://docs.juce.com/master/group__juce__data__structures.html Declared deps: juce_events.
rock_hero_add_external_juce_module_wrapper(
    rock_hero_juce_data_structures juce::juce_data_structures PUBLIC_DEPS rock_hero_juce_events)

# JUCE module declaration and docs:
# https://github.com/juce-framework/JUCE/blob/master/modules/juce_graphics/juce_graphics.h
# https://docs.juce.com/master/group__juce__graphics.html Declared deps: juce_events.
rock_hero_add_external_juce_module_wrapper(rock_hero_juce_graphics juce::juce_graphics PUBLIC_DEPS
                                           rock_hero_juce_events)

# JUCE module declaration and docs:
# https://github.com/juce-framework/JUCE/blob/master/modules/juce_gui_basics/juce_gui_basics.h
# https://docs.juce.com/master/group__juce__gui__basics.html Declared deps: juce_graphics,
# juce_data_structures.
rock_hero_add_external_juce_module_wrapper(
    rock_hero_juce_gui_basics juce::juce_gui_basics PUBLIC_DEPS rock_hero_juce_graphics
    rock_hero_juce_data_structures)

# JUCE module declaration and docs:
# https://github.com/juce-framework/JUCE/blob/master/modules/juce_gui_extra/juce_gui_extra.h
# https://docs.juce.com/master/group__juce__gui__extra.html Declared deps: juce_gui_basics.
rock_hero_add_external_juce_module_wrapper(rock_hero_juce_gui_extra juce::juce_gui_extra
                                           PUBLIC_DEPS rock_hero_juce_gui_basics)

# JUCE module declaration and docs:
# https://github.com/juce-framework/JUCE/blob/master/modules/juce_audio_basics/juce_audio_basics.h
# https://docs.juce.com/master/group__juce__audio__basics.html Declared deps: juce_core.
rock_hero_add_external_juce_module_wrapper(rock_hero_juce_audio_basics juce::juce_audio_basics
                                           PUBLIC_DEPS rock_hero_juce_core)

# JUCE module declaration and docs:
# https://github.com/juce-framework/JUCE/blob/master/modules/juce_audio_devices/juce_audio_devices.h
# https://docs.juce.com/master/group__juce__audio__devices.html Declared deps: juce_audio_basics,
# juce_events.
rock_hero_add_external_juce_module_wrapper(
    rock_hero_juce_audio_devices juce::juce_audio_devices PUBLIC_DEPS rock_hero_juce_audio_basics
    rock_hero_juce_events)

# JUCE module declaration and docs:
# https://github.com/juce-framework/JUCE/blob/master/modules/juce_audio_formats/juce_audio_formats.h
# https://docs.juce.com/master/group__juce__audio__formats.html Declared deps: juce_audio_basics.
rock_hero_add_external_juce_module_wrapper(rock_hero_juce_audio_formats juce::juce_audio_formats
                                           PUBLIC_DEPS rock_hero_juce_audio_basics)

# JUCE module declaration and docs: Source header:
# external/tracktion_engine/modules/juce_audio_processors_headless/ juce_audio_processors_headless.h
# https://docs.juce.com/master/group__juce__audio__processors__headless.html Declared deps:
# juce_audio_basics, juce_events.
rock_hero_add_external_juce_module_wrapper(
    rock_hero_juce_audio_processors_headless juce::juce_audio_processors_headless PUBLIC_DEPS
    rock_hero_juce_audio_basics rock_hero_juce_events)

# JUCE module declaration and docs: Source header:
# external/tracktion_engine/modules/juce_audio_processors/juce_audio_processors.h
# https://docs.juce.com/master/group__juce__audio__processors.html Declared deps: juce_gui_extra,
# juce_audio_processors_headless.
rock_hero_add_external_juce_module_wrapper(
    rock_hero_juce_audio_processors juce::juce_audio_processors PUBLIC_DEPS
    rock_hero_juce_gui_extra rock_hero_juce_audio_processors_headless)

# JUCE module declaration and docs:
# https://github.com/juce-framework/JUCE/blob/master/modules/juce_audio_utils/juce_audio_utils.h
# https://docs.juce.com/master/group__juce__audio__utils.html Declared deps: juce_audio_processors,
# juce_audio_formats, juce_audio_devices.
rock_hero_add_external_juce_module_wrapper(
    rock_hero_juce_audio_utils juce::juce_audio_utils PUBLIC_DEPS rock_hero_juce_audio_processors
    rock_hero_juce_audio_formats rock_hero_juce_audio_devices)

# JUCE module declaration and docs:
# https://github.com/juce-framework/JUCE/blob/master/modules/juce_dsp/juce_dsp.h
# https://docs.juce.com/master/group__juce__dsp.html Declared deps: juce_audio_formats.
rock_hero_add_external_juce_module_wrapper(rock_hero_juce_dsp juce::juce_dsp PUBLIC_DEPS
                                           rock_hero_juce_audio_formats)

# JUCE module declaration and docs:
# https://github.com/juce-framework/JUCE/blob/master/modules/juce_osc/juce_osc.h
# https://docs.juce.com/master/group__juce__osc.html Declared deps: juce_events.
rock_hero_add_external_juce_module_wrapper(rock_hero_juce_osc juce::juce_osc PUBLIC_DEPS
                                           rock_hero_juce_events)

# Tracktion module declaration: Source header:
# external/tracktion_engine/modules/tracktion_core/tracktion_core.h Tracktion docs do not appear to
# expose a dedicated per-module dependency page for tracktion_core the way the module header does,
# so the source declaration is the authoritative dependency link. Declared deps: juce_audio_formats.
rock_hero_add_external_module_wrapper(rock_hero_tracktion_core tracktion::tracktion_core
                                      PUBLIC_DEPS rock_hero_juce_audio_formats)

# Tracktion module declaration: Source header:
# external/tracktion_engine/modules/tracktion_graph/tracktion_graph.h Tracktion docs search does not
# show a dedicated dependency-summary page for tracktion_graph, so the module header remains the
# clearest dependency source. Declared deps: juce_audio_formats. The wrapper also exports
# rock_hero_tracktion_core because tracktion_graph.h includes ../tracktion_core/tracktion_core.h
# directly in its public surface.
rock_hero_add_external_module_wrapper(
    rock_hero_tracktion_graph tracktion::tracktion_graph PUBLIC_DEPS rock_hero_tracktion_core
    rock_hero_juce_audio_formats)

# Tracktion module declaration and docs: Source header:
# external/tracktion_engine/modules/tracktion_engine/tracktion_engine.h
# https://tracktion.github.io/tracktion_engine/group__tracktion__engine.html Declared deps:
# juce_audio_devices, juce_audio_utils, juce_dsp, juce_osc, juce_gui_extra, tracktion_graph. The
# wrapper also exports rock_hero_tracktion_core because tracktion_engine.h includes
# ../tracktion_core/tracktion_core.h directly in its public surface, in addition to the
# tracktion_graph dependency declared by the module metadata.
rock_hero_add_external_module_wrapper(
    rock_hero_tracktion_engine
    tracktion::tracktion_engine
    PUBLIC_DEPS
    rock_hero_tracktion_core
    rock_hero_tracktion_graph
    rock_hero_juce_audio_devices
    rock_hero_juce_audio_utils
    rock_hero_juce_dsp
    rock_hero_juce_osc
    rock_hero_juce_gui_extra)
