cmake_minimum_required(VERSION 3.16)
project(cosmosim C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Compiler flags
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    add_compile_options(-g -O0 -Wall -Wextra)
else()
    add_compile_options(-O2 -Wall -Wextra)
endif()

# GLFW via FetchContent if not found
find_package(glfw3 3.3 QUIET)
if(NOT glfw3_FOUND)
    include(FetchContent)
    set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(
        glfw
        GIT_REPOSITORY https://github.com/glfw/glfw.git
        GIT_TAG 3.4
    )
    FetchContent_MakeAvailable(glfw)
endif()

# GLAD (bundled)
add_library(glad STATIC glad/src/gl.c)
target_include_directories(glad PUBLIC glad/include)

# OpenMP (optional but recommended)
find_package(OpenMP)

# Shader directory define
add_definitions(-DSHADER_DIR="${CMAKE_SOURCE_DIR}/src/shaders/")

# Physics-only library (no renderer, no main — used by tests)
add_library(cosmosim_physics STATIC
    src/octree.c
    src/integrator.c
    src/initial_conditions.c
    src/quasar.c
    src/sph.c
    src/cosmosim_api.c
)
target_include_directories(cosmosim_physics PUBLIC src)
target_link_libraries(cosmosim_physics PRIVATE m)
if(OpenMP_C_FOUND)
    target_link_libraries(cosmosim_physics PRIVATE OpenMP::OpenMP_C)
endif()

# Shared library for UE plugin (physics only, no renderer/OpenGL)
add_library(cosmosim_shared SHARED
    src/octree.c src/integrator.c src/initial_conditions.c
    src/quasar.c src/sph.c src/cosmosim_api.c
)
target_include_directories(cosmosim_shared PUBLIC src/)
target_compile_definitions(cosmosim_shared PRIVATE COSMOSIM_BUILDING_DLL)
set_target_properties(cosmosim_shared PROPERTIES
    OUTPUT_NAME cosmosim
    C_VISIBILITY_PRESET hidden
)
if(OpenMP_C_FOUND)
    target_link_libraries(cosmosim_shared PRIVATE OpenMP::OpenMP_C)
endif()
target_link_libraries(cosmosim_shared PRIVATE m)

# Main executable
add_executable(cosmosim
    src/main.c
    src/renderer.c
)

target_link_libraries(cosmosim PRIVATE cosmosim_physics glad glfw m dl)

# Tests
enable_testing()
add_executable(test_physics tests/test_physics.c)
target_link_libraries(test_physics PRIVATE cosmosim_physics m)
add_test(NAME physics_tests COMMAND test_physics)
