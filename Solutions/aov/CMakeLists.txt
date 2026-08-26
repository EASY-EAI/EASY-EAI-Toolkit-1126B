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
# use c++ 11
#set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fPIC -Wall")
#set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11 -fPIC -Wall")

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
set(common_root    ${toolkit_root}/common)
set(media_root     ${toolkit_root}/media)

# 包含各模块的 API 配置文件，这些文件定义了头文件路径、源文件路径和库文件
include (${common_root}/system_opt/api.cmake)      # 系统基础功能模块
include (${common_root}/data_queue/api.cmake)      # 数据队列模块
include (${media_root}/lmo_adapter/api.cmake)      # LMO 媒体适配器模块
include (${media_root}/aov/api.cmake)              # AOV 专用模块

## api头文件路径
set(AOV_ADAPTER_DIR ${media_root}/lmo_adapter/aov)
set(api_inc
    ${AOV_ADAPTER_DIR}              # AOV 专用通用接口（需在 common/ 前面）
    ${SYSOPT_INCLUDE_DIRS}          # 系统基础功能模块头文件路径
    ${DQUEUE_INCLUDE_DIRS}          # 数据队列模块头文件路径
    ${LOGMANAGER_INCLUDE_DIRS}      # 日志管理模块头文件路径
    ${CAMERA_ADAPTER_INCLUDE_DIRS}  # 摄像头适配器模块头文件路径
    )
## api源文件路径
file(GLOB api_srcs
    ${SYSOPT_SOURCE_DIRS}           # 系统基础功能模块源文件
    ${DQUEUE_SOURCE_DIRS}           # 数据队列模块源文件
    ${CAMERA_ADAPTER_SOURCE_DIRS}   # 摄像头适配器模块源文件
    ${AOV_ADAPTER_DIR}/sample_comm.c        # AOV 专用 common 接口
    # sample_comm_muxer.c removed — raw stream output no longer needs rkmuxer
    )
# 排除 common/sample_comm.c（AOV 用 aov/ 版本替代）
list(FILTER api_srcs EXCLUDE REGEX "common/sample_comm\\.c$")
## api静态库文件路径
link_directories(
    )
## api所依赖的库参数
set(sysLib_list
    ${SYSOPT_LIBS}                  # 系统基础功能模块依赖库
    ${DQUEUE_LIBS}                  # 数据队列模块依赖库
    ${LOGMANAGER_LIBS}              # 日志管理模块依赖库
    ${CAMERA_ADAPTER_LIBS}          # 摄像头适配器模块依赖库
    )
# ======================== easyeai_api =========================

# ========================== custom ============================
# solution project
## 在/usr/share/cmake-3.18/Modules/目录下的Find*.cmake，都能通过这种方式被找到
#find_package(OpenCV REQUIRED)
if(USE_RKAIQ)

    include_directories(
	    ${CMAKE_SYSROOT}/usr/include/rkaiq/
	    ${CMAKE_SYSROOT}/usr/include/rkaiq/uAPI2/
    )
    add_definitions(-DRKAIQ_UAPI2)
    add_definitions(-DRKAIQ)
endif()

## 在/usr/lib/aarch64-linux-gnu/pkgconfig/下的pc文件，都可以通过这种方式查找
## 用法：
## find_package(PkgConfig)
## pkg_search_module(<prefix> REQUIRED [xxxx])
## pkg_search_module(<prefix1> REQUIRED [xxxx])
## pkg_search_module(<prefix2> REQUIRED [xxxx] [xxxxxxxxxxx])
## 备注：
##   <prefix>_FOUND：如果找到模块，此变量为1
##   <prefix>_LIBRARIES：模块的链接库名称（不包含-l）
##   <prefix>_LINK_LIBRARIES：模块的链接库名称（包含绝对路径）
##   <prefix>_LIBRARY_DIRS：模块的链接库路径（不包含-L）
##   <prefix>_INCLUDE_DIRS：模块的头文件路径（不包含-I）
##   <prefix>_LDFLAGS：链接flags
##   <prefix>_LDFLAGS_OTHER：额外的链接flags
##   <prefix>_CFLAGS：所有的编译器cflags
##   <prefix>_CFLAGS_OTHER：其他的编译器cflags
## pkg_check_modules 和 pkg_search_module 的异/同：
##   同：功能都是相同的，而且都能搜索多个模块，如：pkg_check_modules(AAAAA REQUIRED libxml-2.0 libxml2 libxml>=2)
##   异：
##       pkg_check_modules: 必须所有模块都存在，才能生效
##       pkg_search_module: 只要第一个模块存在，就能生效

#find_package(PkgConfig)
#pkg_check_modules(GTK3 REQUIRED gtk+-3.0)
#pkg_search_module(GST1.0 REQUIRED gstreamer-1.0)
#pkg_check_modules(GST1.0_VIDEO REQUIRED gstreamer-video-1.0)

# aov api 根目录（common/core/isp/power/platform/record/muxer/osd/media 已迁移至此）

# -I
set(custom_inc
    ./src/app
    ${AOV_INCLUDE_DIRS}
    ${CMAKE_SYSROOT}/usr/include/rk
)
# -L
link_directories(
   #${PROJECT_SOURCE_DIR}/libs
   ${CMAKE_SYSROOT}/usr/lib
   ${common_root}/log_manager
)
# -l
set(custom_libs
    rga
    rtsp
    rockit
    #${OpenCV_LIBS}
    #${GTK3_LIBRARIES}
    #${GST1.0_LIBRARIES}
    #${GST1.0_VIDEO_LIBRARIES}
)
# source code
aux_source_directory(./src     dir_app)
# ========================== custom ============================


#--------------------------
# execute file
#--------------------------
add_executable(${CURRENT_FOLDER}
    ${dir_app}
    ${api_srcs}
)   #-o
target_include_directories(${CURRENT_FOLDER} PRIVATE ${custom_inc} ${api_inc})
target_link_libraries(${CURRENT_FOLDER} ${AOV_LIBS} ${custom_libs} ${sysLib_list})
if(USE_RKAIQ)
	target_link_libraries(${CURRENT_FOLDER} rkaiq)
endif()
