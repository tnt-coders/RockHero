cmake_minimum_required(VERSION 3.20)
project(RockHero VERSION 0.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(RockHero
    src/main.cpp
)

target_include_directories(RockHero PRIVATE include)
