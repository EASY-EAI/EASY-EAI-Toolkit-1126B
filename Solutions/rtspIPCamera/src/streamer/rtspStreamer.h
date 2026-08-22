/**
 * RTSP 推流模块
 *
 * 基于 easyeai-api 的 RtspServer 封装，接收编码后的 H264/H265 码流并推流。
 *
 * 用法：
 *   RtspStreamer streamer;
 *   streamer.init(8554, "/live/0", "h264");
 *   streamer.pushFrame(data, len, timestamp_us);
 *   ...
 *   streamer.deinit();
 */

#ifndef __RTSP_STREAMER_H__
#define __RTSP_STREAMER_H__

#include <stdint.h>
#include <stdbool.h>
#include <string>

class RtspStreamer
{
public:
    RtspStreamer();
    ~RtspStreamer();

    /**
     * 初始化 RTSP 推流服务
     * @param port   RTSP 端口
     * @param path   URL 路径，如 "/live/0"
     * @param fmt    编码格式 "h264" 或 "h265"
     * @return 0 成功，<0 失败
     */
    int init(int port, const std::string &path, const std::string &fmt);

    /**
     * 推送一帧编码数据
     * @param data      帧数据
     * @param size      数据大小
     * @param timestamp 时间戳（微秒）
     * @return 0 成功，<0 失败
     */
    int pushFrame(const uint8_t *data, int size, uint64_t timestamp);

    void deinit();

    bool isInited() const { return mInited; }

private:
    bool mInited;
    void *mServer; /* RtspServer* */
};

#endif /* __RTSP_STREAMER_H__ */
