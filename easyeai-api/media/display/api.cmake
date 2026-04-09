# source code path
file(GLOB DISPLAY_SOURCE_DIRS 
    ${CMAKE_CURRENT_LIST_DIR}/src/*.c 
    ${CMAKE_CURRENT_LIST_DIR}/src/*.cpp 
    )

# static Library paths
file(GLOB DISPLAY_LIBS_DIRS 
    ${CMAKE_CURRENT_LIST_DIR}
    )
    
# headfile path
set(DISPLAY_INCLUDE_DIRS 
    ${CMAKE_CURRENT_LIST_DIR} 
    ${CMAKE_CURRENT_LIST_DIR}/include 
    )

# c/c++ flags
set(DISPLAY_LIBS 
    display 
    rga 
    drm 
    )
