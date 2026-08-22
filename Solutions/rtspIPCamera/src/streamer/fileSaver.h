/**
 * H264/H265 文件保存模块
 *
 * 将编码后的 H264/H265 码流写入文件，支持 AnnexB 裸流格式。
 *
 * 用法：
 *   FileSaver saver;
 *   saver.init("/userdata/output.h264", "h264");
 *   saver.writeFrame(data, len);
 *   ...
 *   saver.deinit();
 */

#ifndef __FILE_SAVER_H__
#define __FILE_SAVER_H__

#include <stdint.h>
#include <stdbool.h>
#include <string>

class FileSaver
{
public:
    FileSaver();
    ~FileSaver();

    /**
     * 初始化文件保存
     * @param filePath 输出文件路径
     * @param fmt       编码格式 "h264" 或 "h265"
     * @return 0 成功，<0 失败
     */
    int init(const std::string &filePath, const std::string &fmt);

    /**
     * 写入一帧编码数据
     * @param data 帧数据
     * @param size 数据大小
     * @return 0 成功，<0 失败
     */
    int writeFrame(const uint8_t *data, uint32_t size);

    void deinit();

    bool isInited() const { return mInited; }

private:
    bool        mInited;
    FILE       *mFp;
    std::string mFilePath;
    std::string mFmt;
    uint32_t    mFrameCount;
};

#endif /* __FILE_SAVER_H__ */
