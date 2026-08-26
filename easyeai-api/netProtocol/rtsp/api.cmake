# ===================== RTSP Server =====================
# source code path
file(GLOB RTSP_SERVER_SOURCE_DIRS
    ${CMAKE_CURRENT_LIST_DIR}/server/*.c
    ${CMAKE_CURRENT_LIST_DIR}/server/*.cpp
    )

# headfile path
set(RTSP_SERVER_INCLUDE_DIRS
    ${CMAKE_CURRENT_LIST_DIR}/server
    )

# c/c++ flags
set(RTSP_SERVER_LIBS
    pthread
    )

# ===================== RTSP Client =====================
# source code path
file(GLOB RTSP_CLIENT_SOURCE_DIRS
    ${CMAKE_CURRENT_LIST_DIR}/client/*.c
    ${CMAKE_CURRENT_LIST_DIR}/client/*.cpp
    )

# headfile path
set(RTSP_CLIENT_INCLUDE_DIRS
    ${CMAKE_CURRENT_LIST_DIR}/client
    )

# c/c++ flags
set(RTSP_CLIENT_LIBS
    pthread
    )
