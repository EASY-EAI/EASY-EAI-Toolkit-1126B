include(${CMAKE_CURRENT_LIST_DIR}/../../env.cmake)

# source code path
file(GLOB LOGMANAGER_PRO_SOURCE_DIRS
    ${CMAKE_CURRENT_LIST_DIR}/*.c 
    ${CMAKE_CURRENT_LIST_DIR}/*.cpp 
    )

# static Library paths
file(GLOB LOGMANAGER_PRO_LIBS_DIRS
    ${CMAKE_CURRENT_LIST_DIR}/${CMAKE_BOARDSYS}
    )

# headfile path
set(LOGMANAGER_PRO_INCLUDE_DIRS
    ${CMAKE_CURRENT_LIST_DIR} 
    )

# c/c++ flags
set(LOGMANAGER_PRO_LIBS 
    log_manager_pro
    pthread
    )
