/**
 * H264/H265 文件保存模块 —— 实现
 *
 * 将编码后的 H264/H265 裸流（AnnexB 格式）写入文件。
 * 每帧数据包含 start code (00 00 00 01)，可直接用 ffplay 播放。
 */

#include "fileSaver.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

#include "logHandle.h"

FileSaver::FileSaver()
    : mInited(false)
    , mFp(NULL)
    , mFilePath("")
    , mFmt("h264")
    , mFrameCount(0)
{
}

FileSaver::~FileSaver()
{
    deinit();
}

int FileSaver::init(const std::string &filePath, const std::string &fmt)
{
    mFilePath = filePath;
    mFmt = fmt;
    mFrameCount = 0;

    mFp = fopen(filePath.c_str(), "wb");
    if (!mFp) {
        PRINT_ERROR(g_hFile, "cannot open file: %s\n", filePath.c_str());
        return -1;
    }

    mInited = true;
    PRINT_INFO(g_hFile, "ready: %s (%s)\n", filePath.c_str(), fmt.c_str());
    return 0;
}

int FileSaver::writeFrame(const uint8_t *data, uint32_t size)
{
    if (!mInited || !mFp || !data || size == 0) {
        return -1;
    }

    uint32_t written = fwrite(data, 1, size, mFp);
    if (written != size) {
        PRINT_ERROR(g_hFile, "write error: expected %u, got %u\n", size, written);
        return -1;
    }
    fflush(mFp);

    mFrameCount++;
    if ((mFrameCount % 30) == 0) {
        PRINT_DEBUG(g_hFile, "alive: frameCount=%u\n", mFrameCount);
    }

    return 0;
}

void FileSaver::deinit()
{
    if (mFp) {
        int fd = fileno(mFp);
        if (fd >= 0) {
            fsync(fd);
        }
        fclose(mFp);
        mFp = NULL;
    }
    mInited = false;
    PRINT_INFO(g_hFile, "stopped (total frames: %u)\n", mFrameCount);
}
