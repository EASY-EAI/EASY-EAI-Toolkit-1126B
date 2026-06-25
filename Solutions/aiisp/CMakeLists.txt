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
add_definitions(-Wall -Wpointer-arith)

if(ENABLE_FILE_CACHE)
        add_definitions(-DFILE_CACHE)
endif()

if(OS_LINUX)
        add_definitions(-DOS_LINUX)
endif()


# ======================== easyeai_api =========================
set(toolkit_root ${CMAKE_CURRENT_SOURCE_DIR}/../../easyeai-api)
set(common_root       ${toolkit_root}/common)
set(media_root        ${toolkit_root}/media)

include (${common_root}/system_opt/api.cmake)
include (${media_root}/rockit_adapter/api.cmake)

set(api_inc
    ${SYSOPT_INCLUDE_DIRS}
    ${CAMERA_ADAPTER_INCLUDE_DIRS}
    )

file(GLOB api_srcs
    ${SYSOPT_SOURCE_DIRS}
    ${CAMERA_ADAPTER_SOURCE_DIRS}
    )

set(sysLib_list
    ${SYSOPT_LIBS}
    ${CAMERA_ADAPTER_LIBS}
    )
# ======================== easyeai_api =========================

# ========================== custom ============================

if(USE_RKAIQ)
    include_directories(
            ${CMAKE_SYSROOT}/usr/include/rkaiq/
            ${CMAKE_SYSROOT}/usr/include/rkaiq/uAPI2/
    )
    add_definitions(-DRKAIQ_UAPI2)
    add_definitions(-DRKAIQ)
endif()

# -I
set(custom_inc
    ./src
    ${PROJECT_SOURCE_DIR}/include
)
# -L
link_directories(
    ${CMAKE_SYSROOT}/usr/lib
)
# -l
set(custom_libs
    rga
    rtsp
)
# source code
aux_source_directory(./src dir_srcs)
# ========================== custom ============================


#--------------------------
# execute file
#--------------------------
add_executable(${CURRENT_FOLDER} ${dir_srcs} ${api_srcs})
target_include_directories(${CURRENT_FOLDER} PRIVATE ${custom_inc} ${api_inc})
target_link_libraries(${CURRENT_FOLDER} ${custom_libs} ${sysLib_list})
if(USE_RKAIQ)
        target_link_libraries(${CURRENT_FOLDER} rkaiq)
endif()
