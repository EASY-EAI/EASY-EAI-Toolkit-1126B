/**
 * H.264/H.265 文件读取模块 —— 实现
 *
 * 流程：
 *   1. start() 读取整个文件到内存
 *   2. parseAnnexB() 扫描 start code (00 00 01 / 00 00 00 01) 拆分 NAL unit，
 *      按 first_slice_in_pic_flag / AUD 边界将 NAL 分组为 Access Unit（完整帧）
 *   3. readThread 以指定帧率循环回调，文件读完自动从头循环
 */

#include "fileCapturer.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

#include "log_manager_pro.h"

/* ======================== 日志句柄 ======================== */
static log_mgr_t    s_mgr   = LOG_MGR_INVALID;
static log_handle_t s_hLog  = LOG_HANDLE_INVALID;
static log_handle_t getLogHandle()
{
    if (s_hLog < 0) {
        if (s_mgr < 0) s_mgr = log_manager_init("/userdata/logs/vcic_stream_server.ini");
        s_hLog = log_register(s_mgr, "file");
    }
    return s_hLog;
}

/* ======================== 构造 / 析构 ======================== */

FileCapturer::FileCapturer(const std::string &filePath, int framerate,
                           const std::string &fmt)
    : mFilePath(filePath)
    , mFramerate(framerate > 0 ? framerate : 30)
    , mFmt(fmt)
    , mCb(NULL)
    , mUserData(NULL)
    , mThread(0)
    , mRunning(false)
{
}

FileCapturer::~FileCapturer()
{
    stop();
}

void FileCapturer::setFrameCallback(FrameCallback cb, void *userData)
{
    mCb = cb;
    mUserData = userData;
}

/* ======================== AnnexB NAL 扫描 ======================== */

/**
 * 扫描 AnnexB 码流，找到所有 NAL unit 的起始偏移和长度。
 * start code: 0x00 0x00 0x01 (3字节) 或 0x00 0x00 0x00 0x01 (4字节)
 */
static void findNALUnits(const uint8_t *data, size_t size,
                         std::vector<size_t> &offsets,
                         std::vector<size_t> &lengths)
{
    size_t lastNALStart = (size_t)-1;
    size_t i = 0;

    while (i + 3 <= size) {
        if (data[i] == 0x00 && data[i + 1] == 0x00) {
            if (data[i + 2] == 0x01) {
                /* 3-byte start code at i */
                if (lastNALStart != (size_t)-1) {
                    offsets.push_back(lastNALStart);
                    lengths.push_back(i - lastNALStart);
                }
                lastNALStart = i + 3;
                i += 3;
                continue;
            } else if (i + 3 < size &&
                       data[i + 2] == 0x00 && data[i + 3] == 0x01) {
                /* 4-byte start code at i */
                if (lastNALStart != (size_t)-1) {
                    offsets.push_back(lastNALStart);
                    lengths.push_back(i - lastNALStart);
                }
                lastNALStart = i + 4;
                i += 4;
                continue;
            }
        }
        i++;
    }

    /* 最后一个 NAL */
    if (lastNALStart != (size_t)-1 && lastNALStart < size) {
        offsets.push_back(lastNALStart);
        lengths.push_back(size - lastNALStart);
    }
}

/* ======================== Access Unit 分组 ======================== */

int FileCapturer::parseAnnexB(const uint8_t *data, size_t size)
{
    mFrames.clear();

    std::vector<size_t> offsets;
    std::vector<size_t> lengths;
    findNALUnits(data, size, offsets, lengths);

    if (offsets.empty()) {
        PRINT_ERROR(getLogHandle(), "no NAL units found in file\n");
        return -1;
    }

    bool isH265 = (mFmt == "h265");

    PRINT_DEBUG(getLogHandle(), "found %zu NAL units, grouping into AUs (fmt=%s)...\n",
                offsets.size(), isH265 ? "h265" : "h264");

    std::vector<uint8_t> currentAU;
    bool hasVCL = false;

    for (size_t i = 0; i < offsets.size(); i++) {
        const uint8_t *nalData = data + offsets[i];
        size_t nalLen = lengths[i];

        if (nalLen == 0) continue;

        /* ---------- 提取 NAL type ---------- */
        uint8_t nalType = 0;
        bool isVCL = false;
        bool isNewAU = false;

        if (isH265) {
            /* HEVC NAL header = 2 bytes
             * nal_unit_type = (byte0 >> 1) & 0x3F
             * VCL types: 0~31,  non-VCL: 32~40
             * AUD = 35
             */
            nalType = (nalData[0] >> 1) & 0x3F;
            isVCL = (nalType < 32);

            if (nalType == 35) {
                /* AUD → 新 AU 边界 */
                isNewAU = true;
            } else if (isVCL && nalLen > 2) {
                /* first_slice_segment_in_pic_flag = MSB of nalData[2] */
                uint8_t firstSliceFlag = (nalData[2] >> 7) & 1;
                if (firstSliceFlag && hasVCL) {
                    isNewAU = true;
                }
            }
        } else {
            /* H.264 NAL header = 1 byte
             * nal_unit_type = byte0 & 0x1F
             * VCL types: 1~5,  AUD = 9
             */
            nalType = nalData[0] & 0x1F;
            isVCL = (nalType >= 1 && nalType <= 5);

            if (nalType == 9) {
                /* AUD → 新 AU 边界 */
                isNewAU = true;
            } else if (isVCL && nalLen > 1) {
                /* first_slice_in_pic_flag = MSB of nalData[1] */
                uint8_t firstSliceFlag = (nalData[1] >> 7) & 1;
                if (firstSliceFlag && hasVCL) {
                    isNewAU = true;
                }
            }
        }

        /* ---------- 遇到新 AU 边界时保存当前 AU ---------- */
        if (isNewAU && !currentAU.empty()) {
            mFrames.push_back(currentAU);
            currentAU.clear();
            hasVCL = false;
        }

        /* ---------- 将 NAL（含 4 字节 start code）追加到当前 AU ---------- */
        static const uint8_t SC4[] = {0x00, 0x00, 0x00, 0x01};
        currentAU.insert(currentAU.end(), SC4, SC4 + 4);
        currentAU.insert(currentAU.end(), nalData, nalData + nalLen);

        if (isVCL) hasVCL = true;
    }

    /* 最后一个 AU */
    if (!currentAU.empty()) {
        mFrames.push_back(currentAU);
    }

    PRINT_INFO(getLogHandle(), "parsed %zu access units from %zu NAL units\n",
               mFrames.size(), offsets.size());

    return 0;
}

/* ======================== 文件读取 + 解析 ======================== */

int FileCapturer::loadFile()
{
    /* 检查文件是否存在 */
    struct stat st;
    if (stat(mFilePath.c_str(), &st) != 0) {
        PRINT_ERROR(getLogHandle(), "file not found: %s\n", mFilePath.c_str());
        return -1;
    }
    if (st.st_size <= 0) {
        PRINT_ERROR(getLogHandle(), "file is empty: %s\n", mFilePath.c_str());
        return -1;
    }

    /* 读取整个文件到内存 */
    FILE *fp = fopen(mFilePath.c_str(), "rb");
    if (!fp) {
        PRINT_ERROR(getLogHandle(), "cannot open file: %s\n", mFilePath.c_str());
        return -1;
    }

    size_t fileSize = (size_t)st.st_size;
    uint8_t *buf = (uint8_t *)malloc(fileSize);
    if (!buf) {
        PRINT_ERROR(getLogHandle(), "malloc failed (%zu bytes)\n", fileSize);
        fclose(fp);
        return -1;
    }

    size_t bytesRead = fread(buf, 1, fileSize, fp);
    fclose(fp);

    if (bytesRead != fileSize) {
        PRINT_ERROR(getLogHandle(), "short read: expected %zu, got %zu\n",
                    fileSize, bytesRead);
        free(buf);
        return -1;
    }

    PRINT_INFO(getLogHandle(), "loaded %s (%zu bytes), parsing...\n",
               mFilePath.c_str(), fileSize);

    /* 解析 AnnexB 码流 */
    int ret = parseAnnexB(buf, bytesRead);

    free(buf);
    return ret;
}

/* ======================== 读取线程 ======================== */

void *FileCapturer::readThread(void *para)
{
    FileCapturer *self = (FileCapturer *)para;

    if (self->mFrames.empty()) {
        PRINT_ERROR(getLogHandle(), "no frames to send\n");
        return NULL;
    }

    size_t frameIdx = 0;
    useconds_t frameInterval = (useconds_t)(1000000 / self->mFramerate);
    uint32_t send_cnt = 0;

    PRINT_INFO(getLogHandle(), "read thread started (frames=%zu fps=%d interval=%uus)\n",
               self->mFrames.size(), self->mFramerate, frameInterval);

    while (self->mRunning) {
        const std::vector<uint8_t> &frame = self->mFrames[frameIdx];

        FrameDesc_t desc;
        memset(&desc, 0, sizeof(desc));
        /* 文件模式无法从码流中直接获取分辨率信息，
         * desc 留空即可（server 端的回调不使用 desc） */

        if (self->mCb) {
            self->mCb(frame.data(), (uint32_t)frame.size(), &desc, self->mUserData);
        }

        send_cnt++;
        if ((send_cnt % 30) == 0) {
            PRINT_DEBUG(getLogHandle(), "readThread alive: cnt=%u idx=%zu/%zu\n",
                        send_cnt, frameIdx, self->mFrames.size());
        }

        /* 前进到下一帧，文件读完自动循环 */
        frameIdx = (frameIdx + 1) % self->mFrames.size();

        /* 分段休眠以便快速响应停止请求 */
        useconds_t remaining = frameInterval;
        while (remaining > 0 && self->mRunning) {
            useconds_t chunk = (remaining > 10000) ? 10000 : remaining;
            usleep(chunk);
            remaining -= chunk;
        }
    }

    PRINT_DEBUG(getLogHandle(), "read thread exiting (cnt=%u)\n", send_cnt);
    return NULL;
}

/* ======================== start / stop ======================== */

int FileCapturer::start()
{
    if (mRunning) {
        return 0;
    }

    /* 读取文件并解析 */
    if (loadFile() != 0) {
        return -1;
    }

    if (mFrames.empty()) {
        PRINT_ERROR(getLogHandle(), "no frames parsed from file\n");
        return -1;
    }

    mRunning = true;
    pthread_create(&mThread, NULL, readThread, this);

    return 0;
}

void FileCapturer::stop()
{
    if (!mRunning) {
        return;
    }

    mRunning = false;

    if (mThread) {
        pthread_join(mThread, NULL);
        mThread = 0;
    }

    mFrames.clear();
}
