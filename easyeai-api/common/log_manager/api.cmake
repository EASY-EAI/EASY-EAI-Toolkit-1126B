# source code path
file(GLOB LOGMANAGER_SOURCE_DIRS
    ${CMAKE_CURRENT_LIST_DIR}/*.c 
    ${CMAKE_CURRENT_LIST_DIR}/*.cpp 
    )

# static Library paths
file(GLOB LOGMANAGER_LIBS_DIRS
    ${CMAKE_CURRENT_LIST_DIR}
    )

# headfile path
set(LOGMANAGER_INCLUDE_DIRS
    ${CMAKE_CURRENT_LIST_DIR} 
    ${CMAKE_CURRENT_LIST_DIR}/include 
    ${CMAKE_CURRENT_LIST_DIR}/include_internal 
    )

# c/c++ flags
set(LOGMANAGER_LIBS 
    log_manager
    ini
    pthread
    stdc++ 
    )
