# source code path
file(GLOB IPC_SOURCE_DIRS
    ${CMAKE_CURRENT_LIST_DIR}/src/*.c 
    ${CMAKE_CURRENT_LIST_DIR}/src/*.cpp 
    )

# static Library paths
file(GLOB IPC_LIBS_DIRS
    ${CMAKE_CURRENT_LIST_DIR}
    )

# headfile path
set(IPC_INCLUDE_DIRS
    ${CMAKE_CURRENT_LIST_DIR}
    ${CMAKE_CURRENT_LIST_DIR}/include
    ${CMAKE_CURRENT_LIST_DIR}/include_internal
    )

# c/c++ flags
set(IPC_LIBS 
    ipc 
    pthread
    stdc++ 
    )
