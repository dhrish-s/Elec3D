cmake_minimum_required(VERSION 3.10)
project(Elec3D)

set (CMAKE_CXX_STANDARD 17)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# INCLDUE THE DIRECTORIES
include_directories(
    libs/glad/include
    libs/glfw/glfw-3.4/glfw-3.4/include
    libs/imgui/libs/imgui
    libs/imgui/libs/imgui/backends
    libs/json/single_include
    libs/glm
    include


)

# glfww build from source
add_subdirectory(libs/glfw/glfw-3.4/glfw-3.4)

# collecting the source files
file(GLOB SRC
    src/*.cpp
    libs/glad/src/glad.c
    libs/imgui/libs/imgui/*.cpp
    libs/imgui/libs/imgui/backends/imgui_impl_glfw.cpp
    libs/imgui/libs/imgui/backends/imgui_impl_opengl3.cpp

)

find_package(OpenGL REQUIRED)
add_executable(Elec3D ${SRC})
target_link_libraries(Elec3D glfw OpenGL::GL)