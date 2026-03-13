cmake_minimum_required(VERSION 3.10.2)

STRING(REGEX REPLACE ".*/(.*)" "\\1" CURRENT_FOLDER ${CMAKE_CURRENT_SOURCE_DIR})
MESSAGE("current project: " ${CURRENT_FOLDER})

set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_CROSSCOMPILING TRUE)

cmake_host_system_information(RESULT arch_value QUERY OS_PLATFORM)
if((NOT "${arch_value}" STREQUAL "armv7l") AND (NOT "${arch_value}" STREQUAL "aarch64"))
    include ($ENV{HOME}/configs/cross.cmake)
endif()

#若要使用${CMAKE_SYSROOT}变量，project必须要在set(CMAKE_TOOLCHAIN_FILE "${HOST_DIR}/share/buildroot/toolchainfile.cmake")后面设置
project(${CURRENT_FOLDER})
add_definitions(-Wall -Wpointer-arith -Wno-unused-variable)
# use c++ 11
#set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fPIC -Wall")
#set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11 -fPIC -Wall")
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -fuse-ld=gold")
set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -fuse-ld=gold")

# ======================== easyeai_api =========================
set(toolkit_root ${CMAKE_CURRENT_SOURCE_DIR}/../../easyeai-api)
set(algorithm_root ${toolkit_root}/algorithm)
set(common_root    ${toolkit_root}/common)
set(meida_root     ${toolkit_root}/media)

include (${common_root}/base64/api.cmake)
include (${common_root}/system_opt/api.cmake)
include (${common_root}/log_manager/api.cmake)
include (${common_root}/font_engine/api.cmake)
include (${meida_root}/rga/api.cmake)
include (${algorithm_root}/person_detect/api.cmake)
## api头文件路径
set(api_inc 
    ${BASE64_INCLUDE_DIRS} 
    ${SYSOPT_INCLUDE_DIRS} 
    ${LOGMANAGER_INCLUDE_DIRS} 
    ${FONTENGINE_INCLUDE_DIRS} 
    ${RGA_INCLUDE_DIRS} 
    ${PERSON_DETECT_INCLUDE_DIRS} 
    )
## api源文件路径
file(GLOB api_srcs 
    ${BASE64_SOURCE_DIRS} 
    ${SYSOPT_SOURCE_DIRS} 
    ${FONTENGINE_SOURCE_DIRS} 
    ${RGA_SOURCE_DIRS}
    )
## api静态库文件路径
link_directories(
    ${LOGMANAGER_LIBS_DIRS} 
    ${FONTENGINE_LIBS_DIRS} 
    ${PERSON_DETECT_LIBS_DIRS} 
)
## api所依赖的库参数
set(sysLib_list 
    ${BASE64_LIBS} 
    ${SYSOPT_LIBS} 
    ${LOGMANAGER_LIBS} 
    ${FONTENGINE_LIBS} 
    ${RGA_LIBS} 
    ${PERSON_DETECT_LIBS} 
    )
# ======================== easyeai_api =========================


# ========================== custom ============================
# solution project
# -I
set(custom_inc 
    ${CMAKE_SYSROOT}/usr/include/drm
)
# -L
#link_directories(
    #${CAMKE_SYSROOT}/usr/lib/aarch64-linux-gnu/lapack
    #${CAMKE_SYSROOT}/usr/lib/aarch64-linux-gnu/blas
    #${PROJECT_SOURCE_DIR}/libs 
#)
# -l
set(custom_libs
    drm
    rga 
    #opencv_core 
    #opencv_imgproc 
    #opencv_imgcodecs
    #${GTK3_LIBRARIES} 
    #${GST1.0_LIBRARIES} 
    #${GST1.0_VIDEO_LIBRARIES} 
)
# source code
aux_source_directory(./src dir_srcs)
aux_source_directory(./src/camera dir_srcs)
aux_source_directory(./src/display dir_srcs)
# ========================== custom ============================

#--------------------------
# execute file
#--------------------------
add_executable(${CURRENT_FOLDER} ${dir_srcs} ${api_srcs})   #-o
target_include_directories(${CURRENT_FOLDER} PRIVATE ${custom_inc} ${api_inc}) #-I
target_link_libraries(${CURRENT_FOLDER} ${custom_libs} ${sysLib_list}) #-l


