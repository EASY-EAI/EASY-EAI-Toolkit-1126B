# source code path
file(GLOB RGA_SOURCE_DIRS
    ${CMAKE_CURRENT_LIST_DIR}/*.c 
    ${CMAKE_CURRENT_LIST_DIR}/*.cpp 
    )

# headfile path
set(RGA_INCLUDE_DIRS 
    ${CMAKE_CURRENT_LIST_DIR} 
    )

# c/c++ flags
set(RGA_LIBS 
    pthread 
    )
