cmake_minimum_required(VERSION 3.11.0)

set(OpenGL_GL_PREFERENCE GLVND)

# Some compiler flags
set(CMAKE_CXX_STANDARD 17) # C++17
if (MSVC)
    SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}" "/MP /FS")
else()
    # Spare me -Wall's wrath
    set(CXX_ONLY_FLAGS -Wno-class-memaccess -Wno-pessimizing-move)
    add_compile_options("$<$<COMPILE_LANGUAGE:CXX>:${CXX_ONLY_FLAGS}>")
endif()

# Project name
project(ap_gen_tool)

#justwindowsthings
if (WIN32)
    add_definitions(-DNOMINMAX)
    add_definitions(-D_CRT_SECURE_NO_WARNINGS)
endif()

add_subdirectory(onut)
add_subdirectory(src)
