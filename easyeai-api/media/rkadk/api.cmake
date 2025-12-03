# source code path
file(GLOB RKADK_SOURCE_DIRS
    ${CMAKE_CURRENT_LIST_DIR}/*.c 
    ${CMAKE_CURRENT_LIST_DIR}/*.cpp 
    ${CMAKE_CURRENT_LIST_DIR}/isp/*.c 
    ${CMAKE_CURRENT_LIST_DIR}/isp/*.cpp 
    )

# static Library paths
file(GLOB RKADK_LIBS_DIRS 
    ${CMAKE_CURRENT_LIST_DIR}
    ${CMAKE_SYSROOT}/oem/usr/lib
    )

# headfile path
set(RKADK_INCLUDE_DIRS
    ${CMAKE_CURRENT_LIST_DIR} 
    ${CMAKE_SYSROOT}/oem/usr/include/rkadk
    )

# c/c++ flags
set(RKADK_LIBS 
    rkadk
    )
