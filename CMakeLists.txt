cmake_minimum_required(VERSION 3.20)
project(
    RockHero
    VERSION 0.0.1
    LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Set project-config specific options
option(PROJECT_CONFIG_ENABLE_DOCS "Enable docs target when Doxygen is available" ON)
option(PROJECT_CONFIG_ENABLE_CLANG_TIDY "Enable clang-tidy target when run-clang-tidy is available"
       ON)

add_subdirectory(project-config)

# --------------------------------------------------------------------------
# Tracktion Engine (brings JUCE 8 via its own modules/juce submodule)
# --------------------------------------------------------------------------
# EXCLUDE_FROM_ALL prevents Tracktion's examples/benchmarks from building as part of the default
# target (they still exist as named targets).
add_subdirectory(tracktion_engine EXCLUDE_FROM_ALL)

# --------------------------------------------------------------------------
# RockHero
# --------------------------------------------------------------------------
juce_add_gui_app(RockHero PRODUCT_NAME "Rock Hero" VERSION ${PROJECT_VERSION})

target_sources(RockHero PRIVATE src/main.cpp src/AudioEngine.cpp src/EditorWindow.cpp
                                src/WaveformDisplay.cpp)

target_include_directories(RockHero PRIVATE include)

target_link_libraries(
    RockHero
    PRIVATE tracktion::tracktion_core
            tracktion::tracktion_engine
            tracktion::tracktion_graph
            juce::juce_audio_devices
            juce::juce_audio_processors
            juce::juce_audio_utils
            juce::juce_recommended_warning_flags)
