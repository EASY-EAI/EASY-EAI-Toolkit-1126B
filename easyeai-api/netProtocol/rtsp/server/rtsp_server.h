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

/* Client connection state */
typedef enum {
    RTSP_CLIENT_STATE_CLOSED = 0,
    RTSP_CLIENT_STATE_INIT,     /* TCP connected, no RTSP request yet */
    RTSP_CLIENT_STATE_READY,    /* SETUP done */
    RTSP_CLIENT_STATE_PLAYING   /* PLAY issued */
} RtspClientState;

/* Client event types for callback */
typedef enum {
    RTSP_CLIENT_EVENT_CONNECTED = 0,    /* New client TCP connected */
    RTSP_CLIENT_EVENT_SETUP,             /* SETUP completed */
    RTSP_CLIENT_EVENT_PLAYING,           /* PLAY issued */
    RTSP_CLIENT_EVENT_DISCONNECTED,     /* Client disconnected or TEARDOWN */
    RTSP_CLIENT_EVENT_ERROR              /* Send error or protocol error */
} RtspClientEvent;

/* Client info snapshot returned by RtspServer_GetClientInfo */
typedef struct {
    int             index;          /* Slot index [0..MAX_CLIENTS-1] */
    RtspClientState state;
    char            ip[64];          /* Client IP address */
    int             is_tcp;         /* 1 = TCP interleaved, 0 = UDP */
    uint32_t        session_id;
} RtspServerClientInfo;

/* Server runtime statistics */
typedef struct {
    int      client_count;         /* Number of active clients */
    int      playing_count;        /* Number of playing clients */
    uint64_t frames_pushed;       /* Total frames pushed via PushFrame */
    uint64_t bytes_sent;           /* Total bytes sent (RTP payload) */
    uint64_t rtp_packets_sent;     /* Total RTP packets sent */
} RtspServerStats;

/* Forward declaration so the callback typedef can reference RtspServer */
typedef struct RtspServer RtspServer;

/* Server event callback.
 * @param server    Server handle
 * @param client_idx Client slot index, or -1 if not applicable
 * @param event     Event type
 * @param userdata  User opaque pointer */
typedef void (*RtspServerEventCallback)(RtspServer* server, int client_idx,
                                        RtspClientEvent event, void* userdata);

/**
 * @brief Create and start an RTSP server.
 *
 * Listens on the given port, serves the given URL path, and accepts
 * both TCP interleaved and UDP RTP clients.
 *
 * @param port  RTSP listen port (e.g. 8554)
 * @param path  URL path, e.g. "/live/main_stream"
 * @param codec Video codec (H264 or H265)
 * @return Server handle, or NULL on failure.
 */
RtspServer* RtspServer_Create(int port, const char* path, RtspServerCodec codec);

/**
 * @brief Set optional SPS/PPS (or VPS/SPS/PPS for H265) parameter sets.
 *
 * These are prepended to the first I-frame of every new client session
 * so the decoder can initialize.  Pass NULL to clear.
 *
 * For H264:  sps and pps should be NALU data WITHOUT start-code prefix.
 * For H265:  vps, sps, pps should be NALU data WITHOUT start-code prefix.
 *
 * @param server  Server handle
 * @param vps     (H265 only) VPS NALU data, or NULL
 * @param vps_size Size of VPS in bytes
 * @param sps     SPS NALU data, or NULL
 * @param sps_size Size of SPS in bytes
 * @param pps     PPS NALU data, or NULL
 * @param pps_size Size of PPS in bytes
 */
void RtspServer_SetParamSets(RtspServer* server,
                             const uint8_t* vps, int vps_size,
                             const uint8_t* sps, int sps_size,
                             const uint8_t* pps, int pps_size);

/**
 * @brief Set an event callback to be notified of client connect/disconnect etc.
 */
void RtspServer_SetEventCallback(RtspServer* server,
                                RtspServerEventCallback cb, void* userdata);

/**
 * @brief Push a video frame (one access unit) to all playing clients.
 *
 * The frame may contain multiple NALUs separated by start codes
 * (00 00 00 01 or 00 00 01).  Each NALU is fragmented into RTP
 * packets per RFC 6184 (H264) or RFC 7798 (H265).
 *
 * @param server    Server handle
 * @param frame     Frame data (Annex-B byte stream)
 * @param size      Frame size in bytes
 * @param timestamp Presentation timestamp in microseconds (us)
 * @return 0 on success, <0 on failure
 */
int RtspServer_PushFrame(RtspServer* server, const uint8_t* frame,
                         int size, uint64_t timestamp);

/**
 * @brief Query server running state.
 * @return true if the server thread is running, false otherwise.
 */
bool RtspServer_IsRunning(RtspServer* server);

/**
 * @brief Get the number of currently connected clients.
 * @return Client count, or 0 if server is NULL.
 */
int RtspServer_GetClientCount(RtspServer* server);

/**
 * @brief Get a snapshot of one client's info.
 *
 * @param server  Server handle
 * @param index   Client slot index [0..max-1]
 * @param info    Output buffer
 * @return 0 on success, <0 if index out of range
 */
int RtspServer_GetClientInfo(RtspServer* server, int index,
                            RtspServerClientInfo* info);

/**
 * @brief Get runtime statistics.
 * @return 0 on success, <0 on failure.
 */
int RtspServer_GetStats(RtspServer* server, RtspServerStats* stats);

/**
 * @brief Destroy the RTSP server, disconnect all clients, and free resources.
 */
void RtspServer_Destroy(RtspServer* server);

#ifdef __cplusplus
}
#endif

#endif // __COMMON_RTSP_SERVER_H__
