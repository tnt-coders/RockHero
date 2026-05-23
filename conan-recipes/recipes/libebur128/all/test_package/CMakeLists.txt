cmake_minimum_required(VERSION 3.24)

project(test_package C)

find_package(libebur128 CONFIG REQUIRED)

add_executable(test_package src/test_package.c)
target_link_libraries(test_package PRIVATE libebur128::ebur128)
