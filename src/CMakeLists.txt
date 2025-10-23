list(APPEND libs PUBLIC libonut)
list(APPEND includes PUBLIC ./thirdparty/onut/include/)

# ${PROJECT_NAME}.exe, use WinMain on Windows
add_executable(${PROJECT_NAME} WIN32 
    generate.cpp       generate.h
    maps.cpp           maps.h
    data.cpp           data.h
    open_world.cpp
    world_opts.cpp
                       defs.h
    python.cpp         python.hpp
                       message.hpp
                       earcut.hpp
                       zip.hpp
)

# Work dir
set_property(TARGET ${PROJECT_NAME} PROPERTY VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/")

target_include_directories(${PROJECT_NAME} PUBLIC ${includes})
target_link_libraries(${PROJECT_NAME} PUBLIC ${libs})
