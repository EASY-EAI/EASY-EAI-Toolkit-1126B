/**
 * RTSP 推流模块
 *
 * 基于 easyeai-api 的 RtspServer 封装，接收编码后的 H264/H265 码流并推流。
 * 支持注入 SPS/PPS 参数集、客户端状态查询、统计信息等。
 *
 * 用法：
 *   RtspStreamer streamer;
 *   streamer.init(8554, "/live/0", "h264");
 *   streamer.setParamSets(sps, spsLen, pps, ppsLen);
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
     * 设置 SPS/PPS 参数集（H265 还需 VPS）
     * 新接入的客户端在收到首帧 I 帧前会先收到参数集，确保解码器正常初始化。
     * @param vps     (H265) VPS 数据指针（不含 start code），H264 传 NULL
     * @param vpsSize VPS 数据长度
     * @param sps     SPS 数据指针（不含 start code）
     * @param spsSize SPS 数据长度
     * @param pps     PPS 数据指针（不含 start code）
     * @param ppsSize PPS 数据长度
     */
    void setParamSets(const uint8_t *vps, int vpsSize,
                      const uint8_t *sps, int spsSize,
                      const uint8_t *pps, int ppsSize);

    /**
     * 推送一帧编码数据
     * @param data      帧数据（Annex-B 格式，含 start code）
     * @param size      数据大小
     * @param timestamp 时间戳（微秒）
     * @return 0 成功，<0 失败
     */
    int pushFrame(const uint8_t *data, int size, uint64_t timestamp);

    /**
     * 获取当前连接的客户端数量
     */
    int getClientCount();

    /**
     * 获取推流统计信息
     */
    void getStats(uint64_t &framesPushed, uint64_t &bytesSent,
                  uint64_t &rtpPacketsSent);

    void deinit();

    bool isInited() const { return mInited; }

private:
    bool mInited;
    void *mServer; /* RtspServer* */
};

#endif /* __RTSP_STREAMER_H__ */
