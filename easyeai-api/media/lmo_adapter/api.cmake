include(${CMAKE_CURRENT_LIST_DIR}/../../env.cmake)
# ============================================================
# 头文件路径 — 只暴露 lmo_common.h
# ============================================================
set(LMO_ADAPTER_INCLUDE_DIRS
    ${CMAKE_CURRENT_LIST_DIR}
    )

# ============================================================
# 预编译库
# ============================================================
set(LMO_ADAPTER_LIBS
    ${CMAKE_CURRENT_LIST_DIR}/${CMAKE_BOARDSYS}/liblmo_adapter.a
    rockit
    rkaiq
    )

# ============================================================
# 向后兼容
# ============================================================
set(CAMERA_ADAPTER_SOURCE_DIRS)
set(CAMERA_ADAPTER_INCLUDE_DIRS
    ${LMO_ADAPTER_INCLUDE_DIRS}
    )
set(CAMERA_ADAPTER_LIBS
    ${LMO_ADAPTER_LIBS}
    pthread
    )
