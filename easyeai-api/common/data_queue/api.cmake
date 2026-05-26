# source code path
file(GLOB DQUEUE_SOURCE_DIRS
    ${CMAKE_CURRENT_LIST_DIR}/*.c
    ${CMAKE_CURRENT_LIST_DIR}/*.cpp
    )

# headfile path
set(DQUEUE_INCLUDE_DIRS
    ${CMAKE_CURRENT_LIST_DIR}
    )

# c/c++ flags
set(DQUEUE_LIBS
    pthread
    )
