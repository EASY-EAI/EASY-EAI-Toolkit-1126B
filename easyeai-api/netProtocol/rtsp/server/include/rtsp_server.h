#ifndef __COMMON_RTSP_SERVER_H__
#define __COMMON_RTSP_SERVER_H__

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    RTSP_SERVER_CODEC_H264 = 0,
    RTSP_SERVER_CODEC_H265 = 1
} RtspServerCodec;

typedef struct RtspServer RtspServer;

/**
 * @brief Create and start an RTSP server
 * @param port The RTSP port (default is usually 554 or 8554)
 * @param path The URL path (e.g., "/live/main_stream")
 * @param codec The video codec (H264 or H265)
 * @return RtspServer* Handle to the RTSP server, or NULL on failure
 */
RtspServer* RtspServer_Create(int port, const char* path, RtspServerCodec codec);

/**
 * @brief Push a video frame to the RTSP server
 * @param server The RTSP server handle
 * @param frame The video frame data
 * @param size The size of the frame data in bytes
 * @param timestamp The presentation timestamp in microseconds (us)
 * @return int 0 on success, <0 on failure
 */
int RtspServer_PushFrame(RtspServer* server, const uint8_t* frame, int size, uint64_t timestamp);

/**
 * @brief Destroy the RTSP server and free resources
 * @param server The RTSP server handle
 */
void RtspServer_Destroy(RtspServer* server);

#ifdef __cplusplus
}
#endif

#endif // __COMMON_RTSP_SERVER_H__