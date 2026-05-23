include_guard()

# Register project-local Conan recipes before delegating to the shared provider.
#
# The local-recipes-index layout intentionally mirrors ConanCenter's `recipes/<name>/all`
# structure so temporary project recipes can be promoted upstream with minimal churn.
find_program(CONAN_COMMAND conan REQUIRED)

set(ROCK_HERO_LOCAL_CONAN_RECIPES "${CMAKE_SOURCE_DIR}/conan-recipes")

execute_process(
    COMMAND
        "${CONAN_COMMAND}" remote add rock_hero_local_recipes "${ROCK_HERO_LOCAL_CONAN_RECIPES}"
        --index 0 --type local-recipes-index --allowed-packages "libebur128/*" --recipes-only
        --force
    RESULT_VARIABLE ROCK_HERO_LOCAL_CONAN_REMOTE_RESULT
    OUTPUT_VARIABLE ROCK_HERO_LOCAL_CONAN_REMOTE_OUTPUT
    ERROR_VARIABLE ROCK_HERO_LOCAL_CONAN_REMOTE_ERROR
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")

if(NOT ROCK_HERO_LOCAL_CONAN_REMOTE_RESULT EQUAL 0)
    message(FATAL_ERROR "Failed to register Rock Hero local Conan recipes: "
                        "${ROCK_HERO_LOCAL_CONAN_REMOTE_ERROR}")
endif()

include("${CMAKE_SOURCE_DIR}/project-config/cmake-conan/conan_provider.cmake")
