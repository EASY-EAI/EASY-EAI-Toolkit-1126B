# source code path
file(GLOB RTSP_SERVER_SOURCE_DIRS 
    ${CMAKE_CURRENT_LIST_DIR}/server/src/*.c 
    ${CMAKE_CURRENT_LIST_DIR}/server/src/*.cpp 
    )

# headfile path
set(RTSP_SERVER_INCLUDE_DIRS 
    ${CMAKE_CURRENT_LIST_DIR}/server/include 
    )

# c/c++ flags
set(RTSP_SERVER_LIBS 
    pthread
    )
