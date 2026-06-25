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

project(${CURRENT_FOLDER})

# ======================== easyeai_api =========================
set(toolkit_root ${CMAKE_CURRENT_SOURCE_DIR}/../../easyeai-api)
set(algorithm_root ${toolkit_root}/algorithm)

include (${algorithm_root}/speech_recognition/api.cmake)

# easyeai 之外的头文件（从 sysroot 取，板子 NFS 挂载且已安装 libasound2-dev）
if(CMAKE_SYSROOT)
    set(EXTRA_INCLUDE_DIRS "${CMAKE_SYSROOT}/usr/include")
else()
    set(EXTRA_INCLUDE_DIRS "/usr/include")
endif()

# ALSA 库路径：CMAKE_SYSROOT 由 cross.cmake 从 $SYSROOT 环境变量赋值
if(CMAKE_SYSROOT)
    set(ALSA_LIB_DIR "${CMAKE_SYSROOT}/usr/lib/aarch64-linux-gnu")
else()
    set(ALSA_LIB_DIR "/usr/lib/aarch64-linux-gnu")
endif()

## api头文件路径
set(api_inc
    ${SPEECH_RECOGNITION_INCLUDE_DIRS}
    ${EXTRA_INCLUDE_DIRS}
    )

## api源文件路径
file(GLOB api_srcs
    ${SPEECH_RECOGNITION_SOURCE_DIRS}
    )

## api静态库文件路径
link_directories(
   ${SPEECH_RECOGNITION_LIBS_DIRS}
   ${ALSA_LIB_DIR}
)

## 依赖的动态库文件
set(sysLib_list
    ${SPEECH_RECOGNITION_LIBS}
    asound           # ALSA PCM 捕获
    )

#--------------------------
# speech_recognition_live
#--------------------------
add_executable(${CURRENT_FOLDER}
    src/speech_recognition_live.cpp
    ${api_srcs}
    )
target_link_libraries(${CURRENT_FOLDER} ${sysLib_list})
target_include_directories(${CURRENT_FOLDER} PRIVATE ${api_inc})

#--------------------------
# 可调编译参数（由 build.sh 通过 -DCAPTURE_DEBUG=ON 传入）
#   CAPTURE_DEBUG: ON=打印采集峰值排查, OFF=关闭（默认）
#--------------------------
option(CAPTURE_DEBUG "print capture peak for debugging" OFF)
if(CAPTURE_DEBUG)
    add_definitions(-DCAPTURE_DEBUG)
endif()
