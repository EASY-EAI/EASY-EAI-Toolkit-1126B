cmake_minimum_required(VERSION 3.10.2)

STRING(REGEX REPLACE ".*/(.*)" "\\1" CURRENT_FOLDER ${CMAKE_CURRENT_SOURCE_DIR})
MESSAGE("current project: " ${CURRENT_FOLDER})

set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_CROSSCOMPILING TRUE)

cmake_host_system_information(RESULT arch_value QUERY OS_PLATFORM)
message(STATUS "architecture: " ${arch_value})

if((NOT "${arch_value}" STREQUAL "armv7l") AND (NOT "${arch_value}" STREQUAL "aarch64"))
    include ($ENV{HOME}/configs/cross.cmake)
endif()

#若要使用${CMAKE_SYSROOT}变量，project必须要在set(CMAKE_TOOLCHAIN_FILE "${HOST_DIR}/share/buildroot/toolchainfile.cmake")后面设置
project(${CURRENT_FOLDER})
add_definitions(-Wall -Wpointer-arith)
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -fuse-ld=gold")
set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -fuse-ld=gold")

# ======================== easyeai_api =========================
set(toolkit_root ${CMAKE_CURRENT_SOURCE_DIR}/../../easyeai-api)
set(algorithm_root ${toolkit_root}/algorithm)
set(common_root    ${toolkit_root}/common)
set(media_root     ${toolkit_root}/media)
set(netprotocol_root ${toolkit_root}/netProtocol)

include (${common_root}/system_opt/api.cmake)
include (${common_root}/log_manager_pro/api.cmake)
include (${media_root}/rga/api.cmake)
include (${algorithm_root}/person_detect/api.cmake)
include (${netprotocol_root}/rtsp/api.cmake)

## api头文件路径
set(api_inc
    ${SYSOPT_INCLUDE_DIRS}
    ${LOGMANAGER_PRO_INCLUDE_DIRS}
    ${RGA_INCLUDE_DIRS}
    ${PERSON_DETECT_INCLUDE_DIRS}
    ${RTSP_SERVER_INCLUDE_DIRS}
    )
## api源文件路径
file(GLOB api_srcs
    ${SYSOPT_SOURCE_DIRS}
    ${RGA_SOURCE_DIRS}
    ${RTSP_SERVER_SOURCE_DIRS}
    )
## api静态库文件路径
link_directories(
    ${LOGMANAGER_PRO_LIBS_DIRS}
    ${PERSON_DETECT_LIBS_DIRS}
    )
## api所依赖的库参数
set(sysLib_list
    ${SYSOPT_LIBS}
    ${LOGMANAGER_PRO_LIBS}
    ${RGA_LIBS}
    ${PERSON_DETECT_LIBS}
    ${RTSP_SERVER_LIBS}
    )
# ======================== easyeai_api =========================

# ========================== custom ============================
# -I
set(custom_inc
    ${CMAKE_SYSROOT}/usr/include/
    ${CMAKE_SYSROOT}/usr/include/opencv4/
    ${CMAKE_SYSROOT}/usr/include/rk/
    ${PROJECT_SOURCE_DIR}/src
    ${PROJECT_SOURCE_DIR}/src/capturer
    ${PROJECT_SOURCE_DIR}/src/analyzer
    ${PROJECT_SOURCE_DIR}/src/streamer
    )
# -L
link_directories(
    ${CMAKE_SYSROOT}/usr/lib/aarch64-linux-gnu/lapack
    ${CMAKE_SYSROOT}/usr/lib/aarch64-linux-gnu/blas
    )
# -l
set(custom_libs
    rga
    opencv_core
    opencv_imgproc
    opencv_imgcodecs
    rockit
    )
# source code
aux_source_directory(./src dir_srcs)
aux_source_directory(./src/analyzer dir_srcs)
aux_source_directory(./src/capturer dir_srcs)
aux_source_directory(./src/streamer dir_srcs)
# ========================== custom ============================


#--------------------------
# execute file
#--------------------------
add_executable(${CURRENT_FOLDER} ${dir_srcs} ${api_srcs})
target_include_directories(${CURRENT_FOLDER} PRIVATE ${custom_inc} ${api_inc})
target_link_libraries(${CURRENT_FOLDER} ${custom_libs} ${sysLib_list})
