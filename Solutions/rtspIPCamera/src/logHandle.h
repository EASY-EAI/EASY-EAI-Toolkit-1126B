/**
 * 全局日志句柄定义
 *
 * 在 main.cpp 中初始化，各子模块通过 getLogHandle() 获取对应句柄。
 * 使用惰性初始化模式：若 main 未注册则自动 fallback 到 LOG_HANDLE_INVALID。
 */

#ifndef __LOG_HANDLE_H__
#define __LOG_HANDLE_H__

#include "log_manager_pro.h"

/* 各模块名称 */
#define LOG_MODULE_MAIN     "main"
#define LOG_MODULE_CAPTURE  "capture"
#define LOG_MODULE_ANALYZE  "analyzer"
#define LOG_MODULE_RTSP     "rtsp"
#define LOG_MODULE_FILE     "file"

/* 全局管理器句柄（在 main.cpp 中初始化） */
extern log_mgr_t    g_logMgr;
extern log_handle_t g_hMain;
extern log_handle_t g_hCapture;
extern log_handle_t g_hAnalyze;
extern log_handle_t g_hRtsp;
extern log_handle_t g_hFile;

#endif /* __LOG_HANDLE_H__ */
