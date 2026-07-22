cmake_minimum_required(VERSION 3.10.2)

STRING(REGEX REPLACE ".*/(.*)" "\\1" CURRENT_FOLDER ${CMAKE_CURRENT_SOURCE_DIR})
MESSAGE("current project: " ${CURRENT_FOLDER})

set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_CROSSCOMPILING TRUE)

cmake_host_system_information(RESULT arch_value QUERY OS_PLATFORM)
message(STATUS "architecture: " ${arch_value})

if((NOT "${arch_value}" STREQUAL "armv7l") AND (NOT "${arch_value}" STREQUAL "aarch64"))
    if(EXISTS $ENV{HOME}/configs/cross.cmake)
        include ($ENV{HOME}/configs/cross.cmake)
    endif()
endif()

project(${CURRENT_FOLDER})
add_definitions(-Wall -Wpointer-arith -DRV1126B)

# ======================== easyeai_api =========================
set(toolkit_root ${CMAKE_CURRENT_SOURCE_DIR}/../../easyeai-api)
set(common_root    ${toolkit_root}/common)
set(media_root     ${toolkit_root}/media)
set(netprotocol_root ${toolkit_root}/netProtocol)

include (${common_root}/system_opt/api.cmake)
include (${media_root}/lmo_adapter/api.cmake)
include (${netprotocol_root}/rtsp/api.cmake)

set(api_inc
    ${SYSOPT_INCLUDE_DIRS}
    ${CAMERA_ADAPTER_INCLUDE_DIRS}
    ${RTSP_SERVER_INCLUDE_DIRS}
    )
file(GLOB api_srcs
    ${SYSOPT_SOURCE_DIRS}
    ${RTSP_SERVER_SOURCE_DIRS}
    )
link_directories(
    ${CMAKE_SYSROOT}/usr/lib/
)
set(sysLib_list
    ${SYSOPT_LIBS}
    ${CAMERA_ADAPTER_LIBS}
    ${RTSP_SERVER_LIBS}
    rtsp
    )
# ======================== easyeai_api =========================

# ========================== custom ============================
if(RKAIQ)
    include_directories(
        ${CMAKE_SYSROOT}/usr/include/rkaiq/
        ${CMAKE_SYSROOT}/usr/include/rkaiq/uAPI2/
    )
    add_definitions(-DRKAIQ)
endif()

set(custom_inc
    ${PROJECT_SOURCE_DIR}
)
set(custom_libs
    pthread
    stdc++
    dl
)
# ========================== custom ============================

file(GLOB src_files ${PROJECT_SOURCE_DIR}/src/*.c ${PROJECT_SOURCE_DIR}/src/*.cpp)
add_executable(${CURRENT_FOLDER} ${src_files} ${api_srcs})
target_include_directories(${CURRENT_FOLDER} PRIVATE ${custom_inc} ${api_inc})
target_link_libraries(${CURRENT_FOLDER} ${custom_libs} ${sysLib_list})

# ignore unresolved symbols in static library object files.
target_link_options(${CURRENT_FOLDER} PRIVATE -Wl,--unresolved-symbols=ignore-in-object-files)
