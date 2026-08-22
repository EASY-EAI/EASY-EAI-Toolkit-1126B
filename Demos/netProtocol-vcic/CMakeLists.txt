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
add_definitions(-Wall -Wpointer-arith)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11 -fPIC -Wall -pthread")

# ======================== easyeai_api =========================
set(toolkit_root ${CMAKE_CURRENT_SOURCE_DIR}/../../easyeai-api)
set(netProtocol_root ${toolkit_root}/netProtocol)

include (${netProtocol_root}/vcic/api.cmake)

## api头文件路径
set(api_inc
    ${VCIC_INCLUDE_DIRS}
    )
## api静态库文件路径
link_directories(
    ${VCIC_LIBS_DIRS}
    )
## 依赖的库
set(sysLib_list
    ${VCIC_LIBS}
    )

#--------------------------
# test-vcic
#--------------------------
set(demoTag test-vcic)
add_executable(${demoTag} ${demoTag}.cpp)
target_link_libraries(${demoTag} ${sysLib_list})
target_include_directories(${demoTag} PRIVATE ${api_inc})
