/**
 * H.264/H.265 文件读取模块
 *
 * 直接从裸 AnnexB 码流文件（.h264 / .h265 / .264 / .265）读取，
 * 解析为 Access Unit（完整帧）后按指定帧率回调交付上层。
 *
 * 与 RkCapturer 接口对齐，二者在 server 端二选一使用：
 *   - FileCapturer：从文件读取（无需 camera / RK_MPI 采集管线）
 *   - RkCapturer：从 MIPI sensor 实时采集 + RK_MPI 硬件编码
 *
 * 用法：
 *   FileCapturer cap("test.h264", 30, "h264");
 *   cap.setFrameCallback(onFrame, NULL);
 *   cap.start();
 *   ...
 *   cap.stop();
 */

#ifndef __FILE_CAPTURER_H__
#define __FILE_CAPTURER_H__

#include <stdint.h>
#include <stdbool.h>
#include <string>
#include <vector>
#include <pthread.h>

#include "frameTypes.h"     /* 复用 FrameDesc_t / FrameCallback 类型定义 */

class FileCapturer
{
public:
    FileCapturer(const std::string &filePath, int framerate,
                 const std::string &fmt = "h264");
    ~FileCapturer();

    /* 设置帧回调（与 RkCapturer 接口一致） */
    void setFrameCallback(FrameCallback cb, void *userData);

    /* 启动读取线程（读取文件 + 解析 + 循环回调） */
    int start();

    /* 停止读取线程 */
    void stop();

    /* 是否正在运行 */
    bool isRunning() const { return mRunning; }

    /* 获取已解析的帧数 */
    size_t frameCount() const { return mFrames.size(); }

private:
    std::string      mFilePath;
    int              mFramerate;
    std::string      mFmt;
    FrameCallback    mCb;
    void            *mUserData;

    pthread_t        mThread;
    volatile bool    mRunning;

    /* 解析后的 Access Unit 列表 */
    std::vector<std::vector<uint8_t> > mFrames;

    /* 读取文件并解析 */
    int loadFile();

    /* AnnexB 码流解析 → Access Unit 列表 */
    int parseAnnexB(const uint8_t *data, size_t size);

    /* 读取线程入口 */
    static void *readThread(void *para);
};

#endif /* __FILE_CAPTURER_H__ */
