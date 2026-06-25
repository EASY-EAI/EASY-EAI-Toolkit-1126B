# source code path (common)
file(GLOB RKIT_ADAPTER_COMMON_SOURCE_DIRS
    ${CMAKE_CURRENT_LIST_DIR}/common/*.c
    ${CMAKE_CURRENT_LIST_DIR}/common/*.cpp
    )

# source code path (isp3.9)
file(GLOB RKIT_ADAPTER_ISP_SOURCE_DIRS
    ${CMAKE_CURRENT_LIST_DIR}/isp3.9/*.c
    ${CMAKE_CURRENT_LIST_DIR}/isp3.9/*.cpp
    )

set(SDK_INCLUDE_DIR $ENV{SYSROOT}/usr/include)

# headfile path
set(RKIT_ADAPTER_INCLUDE_DIRS
    ${CMAKE_CURRENT_LIST_DIR}/common
    ${CMAKE_CURRENT_LIST_DIR}/isp3.9
    ${SDK_INCLUDE_DIR}
    ${SDK_INCLUDE_DIR}/rkaiq
    ${SDK_INCLUDE_DIR}/rkaiq/uAPI2
    )

# static library search path
set(RKIT_ADAPTER_LIBS_DIRS
    ${CMAKE_CURRENT_LIST_DIR}/libs
    )

# link libraries
set(RKIT_ADAPTER_LIBS
    rockit
    rkaiq
    )

# 向后兼容：Solutions/ 下各子工程引用这些变量名
set(CAMERA_ADAPTER_SOURCE_DIRS
    ${RKIT_ADAPTER_COMMON_SOURCE_DIRS}
    ${RKIT_ADAPTER_ISP_SOURCE_DIRS}
    )
set(CAMERA_ADAPTER_INCLUDE_DIRS
    ${RKIT_ADAPTER_INCLUDE_DIRS}
    )
set(CAMERA_ADAPTER_LIBS
    ${RKIT_ADAPTER_LIBS}
    pthread
    )
