include_guard()

# RockHeroBuildPolicy.cmake
#
# Defines the build-policy targets that Rock Hero-owned targets link to for warning, configuration,
# LTO, and optional warnings-as-errors behavior.
#
# These targets intentionally wrap JUCE's recommended helper targets instead of duplicating their
# flag lists here. Rock Hero is already built around JUCE and Tracktion, and JUCE's defaults are a
# pragmatic baseline for the compiler matrix this project currently supports.
#
# Important exception: `rock_hero_core` is still source-level framework-free. Its public headers and
# implementation must not include JUCE or Tracktion. The only allowed build-time coupling is through
# this file's policy targets. If that coupling ever blocks a core-only build or package, replace the
# implementations below with project-owned flags while keeping the public `rock_hero::build_policy`
# call sites unchanged.

if(NOT TARGET juce::juce_recommended_warning_flags
   OR NOT TARGET juce::juce_recommended_config_flags
   OR NOT TARGET juce::juce_recommended_lto_flags)
    message(
        FATAL_ERROR
            "RockHeroBuildPolicy must be included after external/tracktion_engine creates JUCE's "
            "recommended build flag targets.")
endif()

# ENFORCE A WARNING FREE BUILD BY DEFAULT!
option(ROCK_HERO_WARNINGS_AS_ERRORS "Treat Rock Hero warnings as errors" ON)

# Stable Rock Hero names for JUCE's warning baseline. The wrapper keeps all call sites independent
# of JUCE target names even though the first implementation delegates to JUCE.
add_library(rock_hero_warning_flags INTERFACE)
add_library(rock_hero::warning_flags ALIAS rock_hero_warning_flags)
target_link_libraries(rock_hero_warning_flags INTERFACE juce::juce_recommended_warning_flags)

# Configuration flags cover optimization/debug-info conventions such as JUCE's Debug/Release
# defaults. They stay separately linkable because they are a stronger policy choice than warnings.
add_library(rock_hero_config_flags INTERFACE)
add_library(rock_hero::config_flags ALIAS rock_hero_config_flags)
target_link_libraries(rock_hero_config_flags INTERFACE juce::juce_recommended_config_flags)

# Release LTO is kept separate so wrapper targets can use it without inheriting strict warnings, and
# so first-party targets can opt into the full `rock_hero::build_policy` aggregate.
add_library(rock_hero_lto_flags INTERFACE)
add_library(rock_hero::lto_flags ALIAS rock_hero_lto_flags)
target_link_libraries(rock_hero_lto_flags INTERFACE juce::juce_recommended_lto_flags)

# CMake's portable COMPILE_WARNING_AS_ERROR property is not transitive through
# target_link_libraries(), so this linkable policy target uses compiler flags instead. It is folded
# into `rock_hero::build_policy`, which is only for Rock Hero-owned targets.
add_library(rock_hero_warnings_as_errors INTERFACE)
add_library(rock_hero::warnings_as_errors ALIAS rock_hero_warnings_as_errors)

if(ROCK_HERO_WARNINGS_AS_ERRORS)
    if((CMAKE_CXX_COMPILER_ID STREQUAL "MSVC") OR (CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL
                                                   "MSVC"))
        target_compile_options(rock_hero_warnings_as_errors
                               INTERFACE "$<$<COMPILE_LANGUAGE:CXX>:/WX>")
    elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang|AppleClang|GNU")
        target_compile_options(rock_hero_warnings_as_errors
                               INTERFACE "$<$<COMPILE_LANGUAGE:CXX>:-Werror>")
    endif()
endif()

# Convenience aggregate for Rock Hero-owned libraries, applications, and tests. Do not link this to
# JUCE or Tracktion wrapper targets because it may include warnings-as-errors.
add_library(rock_hero_build_policy INTERFACE)
add_library(rock_hero::build_policy ALIAS rock_hero_build_policy)
target_link_libraries(
    rock_hero_build_policy INTERFACE rock_hero::warning_flags rock_hero::config_flags
                                     rock_hero::lto_flags rock_hero::warnings_as_errors)
