cmake_minimum_required(VERSION 3.11.0)

set(OpenGL_GL_PREFERENCE GLVND)

# Set binary/library output to consistent directories.
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# Some compiler flags
set(CMAKE_CXX_STANDARD 17) # C++17
if (MSVC)
    SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}" "/MP /FS")
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
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
