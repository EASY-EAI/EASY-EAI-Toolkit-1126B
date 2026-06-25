set(SDK_INCLUDE_DIR $ENV{SYSROOT}/usr/include)

# ============================================================
# source code path — common / isp3.9（新版 SDK 方式）
# ============================================================
file(GLOB RKIT_ADAPTER_COMMON_SOURCE_DIRS
    ${CMAKE_CURRENT_LIST_DIR}/common/*.c
    ${CMAKE_CURRENT_LIST_DIR}/common/*.cpp
    )
file(GLOB RKIT_ADAPTER_ISP_SOURCE_DIRS
    ${CMAKE_CURRENT_LIST_DIR}/isp3.9/*.c
    ${CMAKE_CURRENT_LIST_DIR}/isp3.9/*.cpp
    )

# ============================================================
# source code path — platform 核心（底层硬件适配，pipeline 依赖）
# ============================================================
file(GLOB PLATFORM_SYS_SOURCE_DIRS
    ${CMAKE_CURRENT_LIST_DIR}/platform/sys/*.c
    )
file(GLOB PLATFORM_CAM_SOURCE_DIRS
    ${CMAKE_CURRENT_LIST_DIR}/platform/cam/*.c
    )
file(GLOB PLATFORM_AUDIO_SOURCE_DIRS
    ${CMAKE_CURRENT_LIST_DIR}/platform/audio/*.c
    )

# ============================================================
# source code path — Camera 管线 (ISP→VI→VPSS/VENC)
# ============================================================
set(CAMERA_PIPELINE_SOURCE_DIRS
    ${CMAKE_CURRENT_LIST_DIR}/camera/pipeline.c
    )

# ============================================================
# source code path — Audio 管线 (AI→AENC)
# ============================================================
set(AUDIO_PIPELINE_SOURCE_DIRS
    ${CMAKE_CURRENT_LIST_DIR}/audio/pipeline.c
    )

# ============================================================
# 头文件路径
# ============================================================
set(RKIT_ADAPTER_INCLUDE_DIRS
    ${CMAKE_CURRENT_LIST_DIR}/common
    ${CMAKE_CURRENT_LIST_DIR}/isp3.9
    ${CMAKE_CURRENT_LIST_DIR}/camera
    ${CMAKE_CURRENT_LIST_DIR}/audio
    ${CMAKE_CURRENT_LIST_DIR}/platform/cam
    ${CMAKE_CURRENT_LIST_DIR}/platform/audio
    ${CMAKE_CURRENT_LIST_DIR}/platform/sys
    ${SDK_INCLUDE_DIR}
    ${SDK_INCLUDE_DIR}/rkaiq
    ${SDK_INCLUDE_DIR}/rkaiq/uAPI2
    )

# ============================================================
# 静态库搜索路径
# ============================================================
set(RKIT_ADAPTER_LIBS_DIRS
    ${CMAKE_CURRENT_LIST_DIR}/libs
    )

# ============================================================
# 链接库
# ============================================================
set(RKIT_ADAPTER_LIBS
    rockit
    rkaiq
    )

# ============================================================
# 向后兼容：Solutions/ 下各子工程引用这些变量名
# ============================================================
set(CAMERA_ADAPTER_SOURCE_DIRS
    ${RKIT_ADAPTER_COMMON_SOURCE_DIRS}
    ${RKIT_ADAPTER_ISP_SOURCE_DIRS}
    ${PLATFORM_SYS_SOURCE_DIRS}
    ${PLATFORM_CAM_SOURCE_DIRS}
    ${PLATFORM_AUDIO_SOURCE_DIRS}
    ${CAMERA_PIPELINE_SOURCE_DIRS}
    ${AUDIO_PIPELINE_SOURCE_DIRS}
    )
set(CAMERA_ADAPTER_INCLUDE_DIRS
    ${RKIT_ADAPTER_INCLUDE_DIRS}
    )
set(CAMERA_ADAPTER_LIBS
    ${RKIT_ADAPTER_LIBS}
    pthread
    )
