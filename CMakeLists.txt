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

# Main executable
add_executable(cosmosim
    src/main.c
    src/quadtree.c
    src/integrator.c
    src/renderer.c
    src/initial_conditions.c
)

target_link_libraries(cosmosim PRIVATE glad glfw m dl)

if(OpenMP_C_FOUND)
    target_link_libraries(cosmosim PRIVATE OpenMP::OpenMP_C)
endif()
