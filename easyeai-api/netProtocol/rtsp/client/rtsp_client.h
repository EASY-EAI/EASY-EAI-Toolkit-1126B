#ifndef __COMMON_RTSP_CLIENT_H__
#define __COMMON_RTSP_CLIENT_H__

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    RTSP_CLIENT_CODEC_H264 = 0,
    RTSP_CLIENT_CODEC_H265 = 1
} RtspClientCodec;

/* Connection state */
typedef enum {
    RTSP_CLIENT_STATE_IDLE = 0,
    RTSP_CLIENT_STATE_CONNECTING,
    RTSP_CLIENT_STATE_PLAYING,
    RTSP_CLIENT_STATE_RECONNECTING,
    RTSP_CLIENT_STATE_CLOSED
} RtspClientState;

/* Client event types */
typedef enum {
    RTSP_CLIENT_EVENT_CONNECTED = 0,
    RTSP_CLIENT_EVENT_PLAYING,
    RTSP_CLIENT_EVENT_DISCONNECTED,
    RTSP_CLIENT_EVENT_RECONNECTING,
    RTSP_CLIENT_EVENT_ERROR
} RtspClientEvent;

/* Runtime statistics */
typedef struct {
    uint64_t rtp_packets_received;   /* Total RTP packets received */
    uint64_t frames_assembled;        /* Total NALUs/AUs assembled */
    uint64_t bytes_received;           /* Total bytes received from socket */
    uint64_t reconnect_count;         /* Number of reconnections */
    uint32_t last_rtp_seq;            /* Last RTP sequence number */
    uint32_t last_rtp_ts;             /* Last RTP timestamp */
} RtspClientStats;

/* Configuration for advanced options */
typedef struct {
    int  auto_reconnect;       /* 1 = enable auto-reconnect, 0 = disable */
    int  reconnect_interval_ms; /* Reconnect delay in ms (default 3000) */
    int  recv_timeout_ms;      /* Socket recv timeout in ms (default 5000) */
} RtspClientConfig;

/**
 * @brief Frame callback invoked when a complete access unit is assembled.
 *
 * @param nalu      Pointer to the assembled NALU data (with start code prefix).
 * @param size       Size in bytes (including start code).
 * @param timestamp  RTP timestamp (90kHz clock).
 * @param userdata   User opaque pointer.
 */
typedef void (*RtspClientFrameCallback)(const uint8_t* nalu, int size,
                                        uint64_t timestamp, void* userdata);

/* Forward declaration so the callback typedefs can reference RtspClient */
typedef struct RtspClient RtspClient;

/**
 * @brief Event callback for connection state changes.
 *
 * @param client    Client handle
 * @param event     Event type
 * @param userdata  User opaque pointer
 */
typedef void (*RtspClientEventCallback)(RtspClient* client,
                                        RtspClientEvent event,
                                        void* userdata);

/**
 * @brief Create and connect an RTSP client, then start pulling stream.
 *
 * Performs the full RTSP handshake: OPTIONS → DESCRIBE → SETUP → PLAY.
 * Uses TCP interleaved mode (RTP over RTSP) for reliability.
 *
 * @param url       Full RTSP URL, e.g. "rtsp://192.168.1.100:8554/live/0"
 * @param codec     Expected video codec (H264 or H265)
 * @param callback  Called for each assembled NALU (may be NULL)
 * @param userdata  Opaque pointer passed back in callback
 * @return Client handle, or NULL on failure.
 */
RtspClient* RtspClient_Create(const char* url, RtspClientCodec codec,
                              RtspClientFrameCallback callback,
                              void* userdata);

/**
 * @brief Create with advanced configuration (auto-reconnect, timeouts, etc.)
 */
RtspClient* RtspClient_CreateEx(const char* url, RtspClientCodec codec,
                                const RtspClientConfig* config,
                                RtspClientFrameCallback frame_cb,
                                void* frame_userdata,
                                RtspClientEventCallback event_cb,
                                void* event_userdata);

/**
 * @brief Set event callback (can also be set via CreateEx).
 */
void RtspClient_SetEventCallback(RtspClient* client,
                                 RtspClientEventCallback cb,
                                 void* userdata);

/**
 * @brief Check whether the client is connected and streaming.
 * @return true if state == PLAYING, false otherwise.
 */
bool RtspClient_IsConnected(RtspClient* client);

/**
 * @brief Get current connection state.
 */
RtspClientState RtspClient_GetState(RtspClient* client);

/**
 * @brief Get runtime statistics.
 * @return 0 on success, <0 on failure.
 */
int RtspClient_GetStats(RtspClient* client, RtspClientStats* stats);

/**
 * @brief Destroy the client: send TEARDOWN, stop threads, free resources.
 */
void RtspClient_Destroy(RtspClient* client);

#ifdef __cplusplus
}
#endif

#endif // __COMMON_RTSP_CLIENT_H__
