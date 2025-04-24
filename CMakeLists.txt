




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
    include/eigen3/eigen-3.4.0/eigen-3.4.0


)

# glfww build from source
add_subdirectory(libs/glfw/glfw-3.4/glfw-3.4)

# collecting the source files
file(GLOB SRC
    src/*.cpp
    libs/glad/src/glad.c

)

find_package(OpenGL REQUIRED)
add_executable(Elec3D ${SRC})

# adding imgui source files
set(IMGUI_SRC
    libs/imgui/libs/imgui/imgui.cpp
    libs/imgui/libs/imgui/imgui_draw.cpp
    libs/imgui/libs/imgui/imgui_widgets.cpp
    libs/imgui/libs/imgui/imgui_tables.cpp
    libs/imgui/libs/imgui/imgui_demo.cpp
    libs/imgui/libs/imgui/backends/imgui_impl_glfw.cpp
    libs/imgui/libs/imgui/backends/imgui_impl_opengl3.cpp
)

#adding the above files to the source files
target_sources(Elec3D PRIVATE ${IMGUI_SRC})

# adding directories to the include path
target_include_directories(Elec3D PRIVATE
    libs/imgui/libs/imgui
    libs/imgui/libs/imgui/backends
)

target_link_libraries(Elec3D glfw OpenGL::GL)