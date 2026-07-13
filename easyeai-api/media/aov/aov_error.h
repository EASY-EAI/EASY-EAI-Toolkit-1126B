/* ===================== AOV 通用错误码 ===================== */

#ifndef AOV_ERROR_H
#define AOV_ERROR_H

#define AOV_ERR_SUCCESS      0    /* 成功                           */
#define AOV_ERR_FAILED      (-1)  /* 通用失败                        */
#define AOV_ERR_INVALID_ARG (-2)  /* 参数非法                        */
#define AOV_ERR_NOT_INIT    (-3)  /* 模块未初始化                    */
#define AOV_ERR_NULL_PTR    (-4)  /* 空指针                          */
#define AOV_ERR_ABORT       (-5)  /* 操作中止 (状态不匹配)           */
#define AOV_ERR_ALLOC       (-6)  /* 内存分配失败                    */
#define AOV_ERR_IO          (-7)  /* 文件/设备 I/O 错误              */
#define AOV_ERR_NOT_READY   (-8)  /* 尚未就绪                        */
#define AOV_ERR_NOT_MATCH   (-9)  /* 不匹配                          */
#define AOV_ERR_TIMEOUT     (-10) /* 超时                            */
#define AOV_ERR_NO_SPACE    (-11) /* 缓冲区或预分配容量不足          */
#define AOV_ERR_NOT_SUPPORT (-12) /* 不支持                          */
#define AOV_ERR_NOT_ENABLE  (-13) /* 功能未使能                      */

#endif /* AOV_ERROR_H */
