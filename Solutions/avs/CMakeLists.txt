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

#若要使用${CMAKE_SYSROOT}变量，project必须要在set(CMAKE_TOOLCHAIN_FILE "${HOST_DIR}/share/buildroot/toolchainfile.cmake")后面设置
project(${CURRENT_FOLDER})
add_definitions(-Wall -Wpointer-arith)
# use c++ 11
#set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fPIC -Wall")
#set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11 -fPIC -Wall")

if(ENABLE_FILE_CACHE)
	add_definitions(-DFILE_CACHE)
endif()

if(ENABLE_AOV)
	add_definitions(-DENABLE_AOV)
endif()

if(OS_LINUX)
	add_definitions(-DOS_LINUX)
endif()


# ======================== easyeai_api =========================
# 设置 EasyEAI 框架各模块的根目录路径
set(toolkit_root ${CMAKE_CURRENT_SOURCE_DIR}/../../easyeai-api)
set(algorithm_root ${toolkit_root}/algorithm)
set(common_root       ${toolkit_root}/common)
set(media_root        ${toolkit_root}/media)
set(netProtocol_root  ${toolkit_root}/netProtocol)

# 包含各模块的 API 配置文件，这些文件定义了头文件路径、源文件路径和库文件
include (${common_root}/system_opt/api.cmake)      # 系统基础功能模块
include (${common_root}/data_queue/api.cmake)      # 数据队列模块
include (${netProtocol_root}/rtsp/api.cmake) # RTSP 服务模块
include (${media_root}/rockit_adapter/api.cmake)    # Rockchip 媒体适配器模块

# 定义项目所需的头文件包含路径列表
set(api_inc 
    ${SYSOPT_INCLUDE_DIRS}          # 系统基础功能模块头文件路径
    ${DQUEUE_INCLUDE_DIRS}          # 数据队列模块头文件路径
    ${CAMERA_ADAPTER_INCLUDE_DIRS}  # 摄像头适配器模块头文件路径
    ${RTSP_SERVER_INCLUDE_DIRS}     # RTSP 服务模块头文件路径
    )

# 定义项目所需的源文件路径列表
file(GLOB api_srcs 
    ${SYSOPT_SOURCE_DIRS}           # 系统基础功能模块源文件
    ${DQUEUE_SOURCE_DIRS}           # 数据队列模块源文件
    ${CAMERA_ADAPTER_SOURCE_DIRS}   # 摄像头适配器模块源文件
    ${RTSP_SERVER_SOURCE_DIRS}      # RTSP 服务模块源文件
    )

# 定义库文件搜索路径
link_directories( 
    )

# 定义项目依赖的系统库列表
set(sysLib_list 
    ${SYSOPT_LIBS}                  # 系统基础功能模块依赖库
    ${DQUEUE_LIBS}                  # 数据队列模块依赖库
    ${CAMERA_ADAPTER_LIBS}          # 摄像头适配器模块依赖库
    ${RTSP_SERVER_LIBS}             # RTSP 服务模块依赖库
    )

# ========================== custom ============================
if(USE_RKAIQ)
    #find_package(RkAiq REQUIRED)
    include_directories(
	    ${CMAKE_SYSROOT}/usr/include/rkaiq/
	    ${CMAKE_SYSROOT}/usr/include/rkaiq/uAPI2/
    )
    add_definitions(-DRKAIQ_UAPI2)
    add_definitions(-DRKAIQ)
endif()

set(custom_inc
    ./src
    ${PROJECT_SOURCE_DIR}/include
)
# -L
link_directories(
   #${PROJECT_SOURCE_DIR}/libs 
)
# -l
set(custom_libs
    rga
)
# source code
aux_source_directory(./src dir_srcs)

#--------------------------
# execute file
#--------------------------
add_executable(${CURRENT_FOLDER} ${dir_srcs} ${api_srcs})   #-o
target_include_directories(${CURRENT_FOLDER} PRIVATE ${custom_inc} ${api_inc})
target_link_libraries(${CURRENT_FOLDER} ${custom_libs} ${sysLib_list})
if(USE_RKAIQ)
	target_link_libraries(${CURRENT_FOLDER} rkaiq)
endif()
