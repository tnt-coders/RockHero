cmake_minimum_required(VERSION 3.20)
project(
    RockHero
    VERSION 0.0.1
    DESCRIPTION "Guitar-driven rhythm game applications and supporting libraries"
    LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

find_package(PackageBuilder REQUIRED)

include(PackageBuilder)

package_create(PRIMARY_TARGET rock_hero)

# Set project-config specific options
option(PROJECT_CONFIG_ENABLE_DOCS "Enable docs target when Doxygen is available" ON)
option(PROJECT_CONFIG_ENABLE_CLANG_TIDY "Enable clang-tidy target when run-clang-tidy is available"
       ON)

# Limit clang-tidy to project-owned C/C++ sources under apps/ and libs/
string(
    CONCAT _clang_tidy_regex
           # Reject any path containing a build/ segment (generated files)
           [=[^(?!.*[\\/]build[\\/])]=]
           # Advance to the project source root
           [=[.*?[\\/]]=]
           # Must be under apps/ or libs/
           [=[(apps|libs)[\\/]]=]
           # C/C++ source files only
           [=[.*\.(c|cc|cpp|cxx)$]=])
set(PROJECT_CONFIG_CLANG_TIDY_FILE_REGEX
    "${_clang_tidy_regex}"
    CACHE STRING "Limit clang-tidy to project-owned C/C++ sources under apps/ and libs/" FORCE)

add_subdirectory(project-config)

# --------------------------------------------------------------------------
# Tracktion Engine (brings JUCE 8 via its own modules/juce submodule)
# --------------------------------------------------------------------------
# EXCLUDE_FROM_ALL prevents Tracktion's examples/benchmarks from building as part of the default
# target (they still exist as named targets).
set(TE_ADD_EXAMPLES
    OFF
    CACHE BOOL "Disable Tracktion example targets" FORCE)
add_subdirectory(external/tracktion_engine EXCLUDE_FROM_ALL)

# Tracktion/JUCE emits a few warnings from within its own third-party code. Suppress them on the
# third-party interface targets so Rock Hero still gets clean builds without weakening warnings on
# our own code or patching vendor sources just to silence warnings internal to them.
foreach(third_party_target IN ITEMS tracktion_core tracktion_engine tracktion_graph)
    if(TARGET ${third_party_target})
        target_compile_options(
            ${third_party_target} INTERFACE $<$<CXX_COMPILER_ID:MSVC>:/wd4100 /wd4702>
                                            $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-w>)
    endif()
endforeach()

include(RockHeroJuceHelpers)
include(RockHeroExternalModules)

# --------------------------------------------------------------------------
# Tests
# --------------------------------------------------------------------------
include(CTest)

# --------------------------------------------------------------------------
# Libraries
# --------------------------------------------------------------------------
add_subdirectory(libs/rock-hero-core)
add_subdirectory(libs/rock-hero-audio)
add_subdirectory(libs/rock-hero-ui)

# --------------------------------------------------------------------------
# Applications
# --------------------------------------------------------------------------
add_subdirectory(apps/rock-hero-editor)
add_subdirectory(apps/rock-hero)

# Only the game gets a desktop icon/launcher; the editor is a developer tool.
set(PACKAGE_BUILDER_DESKTOP_LINKS rock_hero)
package_install()
