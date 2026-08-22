include(${CMAKE_CURRENT_LIST_DIR}/../../env.cmake)

# source code path
file(GLOB VCIC_SOURCE_DIRS
    ${CMAKE_CURRENT_LIST_DIR}/*.c 
    ${CMAKE_CURRENT_LIST_DIR}/*.cpp 
    )

# static Library paths
file(GLOB VCIC_LIBS_DIRS
    ${CMAKE_CURRENT_LIST_DIR}/${CMAKE_BOARDSYS}
    )

# headfile path
set(VCIC_INCLUDE_DIRS
    ${CMAKE_CURRENT_LIST_DIR} 
    )

# c/c++ flags
set(VCIC_LIBS 
    vcic
    pthread
    )
