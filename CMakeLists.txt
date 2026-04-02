cmake_minimum_required(VERSION 3.20)
project(
    RockHero
    VERSION 0.0.1
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Set project-config specific options
option(PROJECT_CONFIG_ENABLE_DOCS "Enable docs target when Doxygen is available" ON)
option(PROJECT_CONFIG_ENABLE_CLANG_TIDY "Enable clang-tidy target when run-clang-tidy is available"
       ON)

add_subdirectory(project-config)

add_executable(RockHero src/main.cpp)

target_include_directories(RockHero PRIVATE include)
