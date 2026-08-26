#include "rtsp_client.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/select.h>
#include <time.h>
#include <errno.h>

#define RTP_HEADER_SIZE      12
#define MAX_PACKET_SIZE      65536
#define MAX_NALU_SIZE        (4 * 1024 * 1024)
#define RECV_BUF_SIZE        (256 * 1024)

#define H264_NAL_TYPE_FU_A   28
#define H264_NAL_TYPE_STAP_A 24
#define H265_NAL_TYPE_FU     49
#define H265_NAL_TYPE_AP     48

#define DEFAULT_RECONNECT_MS  3000
#define DEFAULT_RECV_TIMEOUT  5000

struct RtspClient {
    /* ---- configuration ---- */
    char               url[512];
    char               host[256];
    int                port;
    char               path[256];
    char               user[128];
    char               pass[128];
    RtspClientCodec    codec;

    RtspClientConfig   config;

    /* ---- callbacks ---- */
    RtspClientFrameCallback  frame_cb;
    void              *frame_userdata;
    RtspClientEventCallback  event_cb;
    void              *event_userdata;

    /* ---- networking ---- */
    int                tcp_fd;
    int                cseq;
    char               session[64];

    /* ---- RTP / NALU reassembly ---- */
    uint8_t           *au_buf;
    int                au_size;
    uint8_t           *frag_buf;
    int                frag_size;
    bool               frag_in_progress;
    uint64_t           au_timestamp;
    bool               seq_inited;

    /* ---- receiver thread ---- */
    pthread_t          thread_id;
    bool               running;
    RtspClientState   state;
    pthread_mutex_t   mutex;

    /* ---- raw TCP receive buffer ---- */
    uint8_t           *recv_buf;
    int                recv_len;

    /* ---- statistics ---- */
    uint64_t           rtp_packets_received;
    uint64_t           frames_assembled;
    uint64_t           bytes_received;
    uint64_t           reconnect_count;
    uint32_t           last_rtp_seq;
    uint32_t           last_rtp_ts;
};

/* ========================================================================
 *  Forward declarations
 * ======================================================================== */
static void* rtsp_client_thread(void* arg);
static void notify_event(RtspClient* c, RtspClientEvent evt);
static void au_flush(RtspClient* c);

/* ========================================================================
 *  URL parsing
 * ======================================================================== */
static int parse_url(RtspClient* c)
{
    const char* p = c->url;
    if (strncmp(p, "rtsp://", 7) != 0) {
        fprintf(stderr, "[RtspClient] Invalid URL scheme\n");
        return -1;
    }
    p += 7;

    /* user:pass@ (optional) */
    const char* at = strchr(p, '@');
    const char* slash = strchr(p, '/');
    if (at && (!slash || at < slash)) {
        const char* colon = strchr(p, ':');
        if (colon && colon < at) {
            int ulen = (int)(colon - p);
            if (ulen >= (int)sizeof(c->user)) ulen = sizeof(c->user) - 1;
            memcpy(c->user, p, ulen);
            c->user[ulen] = '\0';

            int plen = (int)(at - colon - 1);
            if (plen >= (int)sizeof(c->pass)) plen = sizeof(c->pass) - 1;
            memcpy(c->pass, colon + 1, plen);
            c->pass[plen] = '\0';
        } else {
            int ulen = (int)(at - p);
            if (ulen >= (int)sizeof(c->user)) ulen = sizeof(c->user) - 1;
            memcpy(c->user, p, ulen);
            c->user[ulen] = '\0';
        }
        p = at + 1;
    }

    /* host[:port] */
    const char* path_start = strchr(p, '/');
    const char* colon = strchr(p, ':');
    if (colon && (!path_start || colon < path_start)) {
        int hlen = (int)(colon - p);
        if (hlen >= (int)sizeof(c->host)) hlen = sizeof(c->host) - 1;
        memcpy(c->host, p, hlen);
        c->host[hlen] = '\0';
        c->port = atoi(colon + 1);
        if (c->port <= 0) c->port = 554;
    } else {
        int hlen;
        if (path_start) hlen = (int)(path_start - p);
        else hlen = (int)strlen(p);
        if (hlen >= (int)sizeof(c->host)) hlen = sizeof(c->host) - 1;
        memcpy(c->host, p, hlen);
        c->host[hlen] = '\0';
        c->port = 554;
    }

    /* path */
    if (path_start) {
        int plen = (int)strlen(path_start);
        if (plen >= (int)sizeof(c->path)) plen = sizeof(c->path) - 1;
        memcpy(c->path, path_start, plen);
        c->path[plen] = '\0';
    } else {
        c->path[0] = '/';
        c->path[1] = '\0';
    }

    return 0;
}

/* ========================================================================
 *  TCP connection
 * ======================================================================== */
static int connect_tcp(const char* host, int port, int timeout_ms)
{
    struct addrinfo hints, *res, *rp;
    char port_str[16];
    int fd = -1;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family   = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    snprintf(port_str, sizeof(port_str), "%d", port);
    int err = getaddrinfo(host, port_str, &hints, &res);
    if (err != 0) {
        fprintf(stderr, "[RtspClient] getaddrinfo(%s:%d): %s\n",
                host, port, gai_strerror(err));
        return -1;
    }

    for (rp = res; rp != NULL; rp = rp->ai_next) {
        fd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (fd < 0) continue;

        struct timeval tv = {
            .tv_sec  = timeout_ms / 1000,
            .tv_usec = (timeout_ms % 1000) * 1000
        };
        setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

        int flag = 1;
        setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag));

        if (connect(fd, rp->ai_addr, rp->ai_addrlen) == 0) {
            break;
        }
        close(fd);
        fd = -1;
    }

    freeaddrinfo(res);
    return fd;
}

/* ========================================================================
 *  Base64 encoding
 * ======================================================================== */
static void base64_encode(const char* src, char* dst, int dst_size)
{
    static const char tbl[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    int srclen = (int)strlen(src);
    int i = 0, o = 0;
    while (i < srclen && o + 4 < dst_size) {
        uint32_t v;
        int rem = srclen - i;
        if (rem >= 3) {
            v = ((uint32_t)(uint8_t)src[i] << 16) |
                ((uint32_t)(uint8_t)src[i+1] << 8) |
                (uint32_t)(uint8_t)src[i+2];
            dst[o++] = tbl[(v >> 18) & 0x3F];
            dst[o++] = tbl[(v >> 12) & 0x3F];
            dst[o++] = tbl[(v >> 6) & 0x3F];
            dst[o++] = tbl[v & 0x3F];
            i += 3;
        } else if (rem == 2) {
            v = ((uint32_t)(uint8_t)src[i] << 16) |
                ((uint32_t)(uint8_t)src[i+1] << 8);
            dst[o++] = tbl[(v >> 18) & 0x3F];
            dst[o++] = tbl[(v >> 12) & 0x3F];
            dst[o++] = tbl[(v >> 6) & 0x3F];
            dst[o++] = '=';
            i += 2;
        } else {
            v = (uint32_t)(uint8_t)src[i] << 16;
            dst[o++] = tbl[(v >> 18) & 0x3F];
            dst[o++] = tbl[(v >> 12) & 0x3F];
            dst[o++] = '=';
            dst[o++] = '=';
            i += 1;
        }
    }
    dst[o] = '\0';
}

/* ========================================================================
 *  RTSP request/response
 * ======================================================================== */
static int send_request(RtspClient* c, const char* method,
                        const char* extra_headers)
{
    char req[2048];
    int offset = 0;

    offset += snprintf(req + offset, sizeof(req) - offset,
                      "%s rtsp://%s:%d%s RTSP/1.0\r\n"
                      "CSeq: %d\r\n",
                      method, c->host, c->port, c->path, c->cseq);

    if (c->user[0] != '\0') {
        char auth_str[512];
        char b64[768];
        snprintf(auth_str, sizeof(auth_str), "%s:%s", c->user, c->pass);
        base64_encode(auth_str, b64, sizeof(b64));
        offset += snprintf(req + offset, sizeof(req) - offset,
                           "Authorization: Basic %s\r\n", b64);
    }

    if (c->session[0] != '\0') {
        offset += snprintf(req + offset, sizeof(req) - offset,
                           "Session: %s\r\n", c->session);
    }

    offset += snprintf(req + offset, sizeof(req) - offset,
                       "User-Agent: EasyEAI RtspClient/1.0\r\n");

    if (extra_headers) {
        offset += snprintf(req + offset, sizeof(req) - offset, "%s",
                           extra_headers);
    }

    offset += snprintf(req + offset, sizeof(req) - offset, "\r\n");

    int total = offset;
    int sent = 0;
    while (sent < total) {
        int n = (int)send(c->tcp_fd, req + sent, total - sent, 0);
        if (n <= 0) {
            fprintf(stderr, "[RtspClient] send(%s) failed: %s\n",
                    method, strerror(errno));
            return -1;
        }
        sent += n;
    }

    c->cseq++;
    return 0;
}

static int recv_response(RtspClient* c, char* buf, int buf_size)
{
    int total = 0;
    bool header_done = false;
    int content_length = 0;
    int header_end = 0;

    while (total < buf_size - 1) {
        int n = (int)recv(c->tcp_fd, buf + total, buf_size - 1 - total, 0);
        if (n <= 0) {
            fprintf(stderr, "[RtspClient] recv failed: %s\n", strerror(errno));
            return -1;
        }
        total += n;
        buf[total] = '\0';

        if (!header_done) {
            char* eoh = strstr(buf, "\r\n\r\n");
            if (eoh) {
                header_done = true;
                header_end = (int)(eoh - buf) + 4;

                char* cl = strstr(buf, "Content-Length:");
                if (cl) sscanf(cl, "Content-Length: %d", &content_length);

                char* sess = strstr(buf, "Session:");
                if (sess && c->session[0] == '\0') {
                    char tmp[128];
                    sscanf(sess, "Session: %127s", tmp);
                    char* semi = strchr(tmp, ';');
                    if (semi) *semi = '\0';
                    strncpy(c->session, tmp, sizeof(c->session) - 1);
                    c->session[sizeof(c->session) - 1] = '\0';
                }
            }
        }

        if (header_done) {
            int body_recv = total - header_end;
            if (body_recv >= content_length) {
                return total;
            }
        }
    }

    return -1;
}

static int rtsp_options(RtspClient* c)
{
    if (send_request(c, "OPTIONS", NULL) < 0) return -1;
    char resp[2048];
    if (recv_response(c, resp, sizeof(resp)) < 0) return -1;
    int status = 0;
    sscanf(resp, "RTSP/1.0 %d", &status);
    return (status == 200) ? 0 : -1;
}

static int rtsp_describe(RtspClient* c)
{
    if (send_request(c, "DESCRIBE", "Accept: application/sdp\r\n") < 0)
        return -1;
    char resp[4096];
    if (recv_response(c, resp, sizeof(resp)) < 0) return -1;
    int status = 0;
    sscanf(resp, "RTSP/1.0 %d", &status);
    return (status == 200) ? 0 : -1;
}

static int rtsp_setup(RtspClient* c)
{
    char extra[256];
    snprintf(extra, sizeof(extra),
             "Transport: RTP/AVP/TCP;unicast;interleaved=0-1\r\n");
    if (send_request(c, "SETUP", extra) < 0) return -1;
    char resp[2048];
    if (recv_response(c, resp, sizeof(resp)) < 0) return -1;
    int status = 0;
    sscanf(resp, "RTSP/1.0 %d", &status);
    return (status == 200) ? 0 : -1;
}

static int rtsp_play(RtspClient* c)
{
    if (send_request(c, "PLAY", "Range: npt=0.000-\r\n") < 0) return -1;
    char resp[2048];
    if (recv_response(c, resp, sizeof(resp)) < 0) return -1;
    int status = 0;
    sscanf(resp, "RTSP/1.0 %d", &status);
    return (status == 200) ? 0 : -1;
}

static void rtsp_teardown(RtspClient* c)
{
    if (c->session[0] == '\0') return;
    send_request(c, "TEARDOWN", NULL);
    char resp[1024];
    recv_response(c, resp, sizeof(resp));
}

/* ========================================================================
 *  NALU assembly
 * ======================================================================== */
static void au_flush(RtspClient* c)
{
    if (c->au_size > 0 && c->frame_cb) {
        c->frame_cb(c->au_buf, c->au_size, c->au_timestamp, c->frame_userdata);
        c->frames_assembled++;
    }
    c->au_size = 0;
}

static void au_append(RtspClient* c, const uint8_t* data, int len,
                      bool start_code_prefix)
{
    int need = len + (start_code_prefix ? 4 : 0);
    if (c->au_size + need > MAX_NALU_SIZE) {
        fprintf(stderr, "[RtspClient] AU overflow, flushing\n");
        au_flush(c);
    }

    if (start_code_prefix) {
        c->au_buf[c->au_size++] = 0x00;
        c->au_buf[c->au_size++] = 0x00;
        c->au_buf[c->au_size++] = 0x00;
        c->au_buf[c->au_size++] = 0x01;
    }
    memcpy(c->au_buf + c->au_size, data, len);
    c->au_size += len;
}

/* ========================================================================
 *  RTP processing
 * ======================================================================== */
static void process_rtp_h264(RtspClient* c, const uint8_t* rtp, int rtp_len)
{
    if (rtp_len < RTP_HEADER_SIZE + 1) return;

    bool padding   = (rtp[0] >> 5) & 0x01;
    bool extension = (rtp[0] >> 4) & 0x01;
    uint8_t cc     = rtp[0] & 0x0F;
    bool marker    = (rtp[1] >> 7) & 0x01;
    uint16_t seq   = ((uint16_t)rtp[2] << 8) | rtp[3];
    uint32_t ts    = ((uint32_t)rtp[4] << 24) | ((uint32_t)rtp[5] << 16) |
                     ((uint32_t)rtp[6] << 8) | rtp[7];

    c->last_rtp_seq = seq;
    c->last_rtp_ts  = ts;

    int hdr_len = RTP_HEADER_SIZE + 4 * cc;
    if (extension) {
        if (rtp_len < hdr_len + 4) return;
        int ext_len = ((uint16_t)rtp[hdr_len + 2] << 8) | rtp[hdr_len + 3];
        hdr_len += 4 + ext_len * 4;
    }
    if (rtp_len < hdr_len + 1) return;

    const uint8_t* payload = rtp + hdr_len;
    int payload_len = rtp_len - hdr_len;

    if (padding) {
        int pad = rtp[rtp_len - 1];
        payload_len -= pad;
        if (payload_len <= 0) return;
    }

    uint8_t nalu_type = payload[0] & 0x1F;

    if (!c->seq_inited) {
        c->au_timestamp = ts;
        c->seq_inited = true;
    } else if (ts != c->au_timestamp) {
        if (c->frag_in_progress) {
            c->frag_in_progress = false;
            c->frag_size = 0;
        }
        au_flush(c);
        c->au_timestamp = ts;
    }

    if (nalu_type >= 1 && nalu_type <= 23) {
        au_append(c, payload, payload_len, true);
        if (marker) au_flush(c);
    } else if (nalu_type == H264_NAL_TYPE_STAP_A) {
        int offset = 1;
        while (offset + 2 <= payload_len) {
            int sz = (payload[offset] << 8) | payload[offset + 1];
            offset += 2;
            if (offset + sz > payload_len) break;
            au_append(c, payload + offset, sz, true);
            offset += sz;
        }
        if (marker) au_flush(c);
    } else if (nalu_type == H264_NAL_TYPE_FU_A) {
        if (payload_len < 2) return;
        uint8_t fu_ind = payload[0];
        uint8_t fu_hdr = payload[1];
        bool start = (fu_hdr >> 7) & 0x01;
        bool end   = (fu_hdr >> 6) & 0x01;
        uint8_t orig_type = fu_hdr & 0x1F;

        const uint8_t* frag_data = payload + 2;
        int frag_len = payload_len - 2;

        if (start) {
            if (c->frag_in_progress) au_flush(c);
            c->frag_in_progress = true;
            c->frag_size = 0;
            uint8_t recon = (fu_ind & 0xE0) | orig_type;
            c->frag_buf[c->frag_size++] = recon;
            memcpy(c->frag_buf + c->frag_size, frag_data, frag_len);
            c->frag_size += frag_len;
        } else if (c->frag_in_progress) {
            memcpy(c->frag_buf + c->frag_size, frag_data, frag_len);
            c->frag_size += frag_len;
        } else {
            return;
        }

        if (end && c->frag_in_progress) {
            au_append(c, c->frag_buf, c->frag_size, true);
            c->frag_in_progress = false;
            c->frag_size = 0;
            if (marker) au_flush(c);
        }

        if (c->frag_size > MAX_NALU_SIZE - 4) {
            c->frag_in_progress = false;
            c->frag_size = 0;
        }
    }
}

static void process_rtp_h265(RtspClient* c, const uint8_t* rtp, int rtp_len)
{
    if (rtp_len < RTP_HEADER_SIZE + 2) return;

    bool padding   = (rtp[0] >> 5) & 0x01;
    bool extension = (rtp[0] >> 4) & 0x01;
    uint8_t cc     = rtp[0] & 0x0F;
    bool marker    = (rtp[1] >> 7) & 0x01;
    uint16_t seq   = ((uint16_t)rtp[2] << 8) | rtp[3];
    uint32_t ts    = ((uint32_t)rtp[4] << 24) | ((uint32_t)rtp[5] << 16) |
                     ((uint32_t)rtp[6] << 8) | rtp[7];

    c->last_rtp_seq = seq;
    c->last_rtp_ts  = ts;

    int hdr_len = RTP_HEADER_SIZE + 4 * cc;
    if (extension) {
        if (rtp_len < hdr_len + 4) return;
        int ext_len = ((uint16_t)rtp[hdr_len + 2] << 8) | rtp[hdr_len + 3];
        hdr_len += 4 + ext_len * 4;
    }
    if (rtp_len < hdr_len + 2) return;

    const uint8_t* payload = rtp + hdr_len;
    int payload_len = rtp_len - hdr_len;

    if (padding) {
        int pad = rtp[rtp_len - 1];
        payload_len -= pad;
        if (payload_len <= 0) return;
    }

    uint8_t nal_type = (payload[0] & 0x7E) >> 1;

    if (!c->seq_inited) {
        c->au_timestamp = ts;
        c->seq_inited = true;
    } else if (ts != c->au_timestamp) {
        if (c->frag_in_progress) {
            c->frag_in_progress = false;
            c->frag_size = 0;
        }
        au_flush(c);
        c->au_timestamp = ts;
    }

    if (nal_type < H265_NAL_TYPE_AP) {
        au_append(c, payload, payload_len, true);
        if (marker) au_flush(c);
    } else if (nal_type == H265_NAL_TYPE_AP) {
        int offset = 2;
        while (offset + 2 <= payload_len) {
            int sz = (payload[offset] << 8) | payload[offset + 1];
            offset += 2;
            if (offset + sz > payload_len) break;
            au_append(c, payload + offset, sz, true);
            offset += sz;
        }
        if (marker) au_flush(c);
    } else if (nal_type == H265_NAL_TYPE_FU) {
        if (payload_len < 3) return;
        uint8_t fu_hdr = payload[2];
        bool start = (fu_hdr >> 7) & 0x01;
        bool end   = (fu_hdr >> 6) & 0x01;
        uint8_t orig_type = fu_hdr & 0x3F;

        const uint8_t* frag_data = payload + 3;
        int frag_len = payload_len - 3;

        if (start) {
            if (c->frag_in_progress) au_flush(c);
            c->frag_in_progress = true;
            c->frag_size = 0;
            uint8_t hdr0 = (payload[0] & 0x81) | (orig_type << 1);
            uint8_t hdr1 = payload[1];
            c->frag_buf[c->frag_size++] = hdr0;
            c->frag_buf[c->frag_size++] = hdr1;
            memcpy(c->frag_buf + c->frag_size, frag_data, frag_len);
            c->frag_size += frag_len;
        } else if (c->frag_in_progress) {
            memcpy(c->frag_buf + c->frag_size, frag_data, frag_len);
            c->frag_size += frag_len;
        } else {
            return;
        }

        if (end && c->frag_in_progress) {
            au_append(c, c->frag_buf, c->frag_size, true);
            c->frag_in_progress = false;
            c->frag_size = 0;
            if (marker) au_flush(c);
        }

        if (c->frag_size > MAX_NALU_SIZE - 4) {
            c->frag_in_progress = false;
            c->frag_size = 0;
        }
    }
}

static void process_rtp(RtspClient* c, const uint8_t* rtp, int rtp_len)
{
    if (rtp_len < RTP_HEADER_SIZE) return;
    c->rtp_packets_received++;

    if (c->codec == RTSP_CLIENT_CODEC_H265)
        process_rtp_h265(c, rtp, rtp_len);
    else
        process_rtp_h264(c, rtp, rtp_len);
}

/* ========================================================================
 *  Receiver thread — interleaved RTP + auto-reconnect
 * ======================================================================== */
static void* rtsp_client_thread(void* arg)
{
    RtspClient* c = (RtspClient*)arg;
    uint8_t* pkt = (uint8_t*)malloc(MAX_PACKET_SIZE);
    if (!pkt) return NULL;

    bool first_connect = true;

    while (c->running) {
        /* ---- Connect & handshake (or reconnect) ---- */
        if (!first_connect) {
            /* Cleanup previous connection */
            if (c->tcp_fd > 0) {
                close(c->tcp_fd);
                c->tcp_fd = -1;
            }
            c->session[0] = '\0';
            c->seq_inited = false;
            c->frag_in_progress = false;
            c->frag_size = 0;
            c->au_size = 0;
            c->recv_len = 0;

            if (c->config.auto_reconnect) {
                int delay = c->config.reconnect_interval_ms;
                if (delay <= 0) delay = DEFAULT_RECONNECT_MS;

                pthread_mutex_lock(&c->mutex);
                c->state = RTSP_CLIENT_STATE_RECONNECTING;
                pthread_mutex_unlock(&c->mutex);
                notify_event(c, RTSP_CLIENT_EVENT_RECONNECTING);

                c->reconnect_count++;
                printf("[RtspClient] Reconnecting in %dms (attempt %llu)...\n",
                       delay, (unsigned long long)c->reconnect_count);

                /* Sleep with running check */
                int slept = 0;
                while (c->running && slept < delay) {
                    usleep(100000);
                    slept += 100;
                }
                if (!c->running) break;
            } else {
                break; /* No auto-reconnect, exit */
            }
        }
        first_connect = false;

        /* ---- Connect TCP ---- */
        pthread_mutex_lock(&c->mutex);
        c->state = RTSP_CLIENT_STATE_CONNECTING;
        pthread_mutex_unlock(&c->mutex);

        c->tcp_fd = connect_tcp(c->host, c->port, c->config.recv_timeout_ms);
        if (c->tcp_fd < 0) {
            fprintf(stderr, "[RtspClient] Connect to %s:%d failed: %s\n",
                    c->host, c->port, strerror(errno));
            notify_event(c, RTSP_CLIENT_EVENT_ERROR);
            continue;
        }

        c->bytes_received = 0;
        printf("[RtspClient] Connected to %s:%d\n", c->host, c->port);
        notify_event(c, RTSP_CLIENT_EVENT_CONNECTED);

        /* ---- RTSP handshake ---- */
        if (rtsp_options(c) != 0) {
            fprintf(stderr, "[RtspClient] OPTIONS failed\n");
            notify_event(c, RTSP_CLIENT_EVENT_ERROR);
            continue;
        }
        if (rtsp_describe(c) != 0) {
            fprintf(stderr, "[RtspClient] DESCRIBE failed\n");
            notify_event(c, RTSP_CLIENT_EVENT_ERROR);
            continue;
        }
        if (rtsp_setup(c) != 0) {
            fprintf(stderr, "[RtspClient] SETUP failed\n");
            notify_event(c, RTSP_CLIENT_EVENT_ERROR);
            continue;
        }
        if (rtsp_play(c) != 0) {
            fprintf(stderr, "[RtspClient] PLAY failed\n");
            notify_event(c, RTSP_CLIENT_EVENT_ERROR);
            continue;
        }

        pthread_mutex_lock(&c->mutex);
        c->state = RTSP_CLIENT_STATE_PLAYING;
        pthread_mutex_unlock(&c->mutex);

        printf("[RtspClient] Stream started (%s)\n",
               c->codec == RTSP_CLIENT_CODEC_H265 ? "H265" : "H264");
        notify_event(c, RTSP_CLIENT_EVENT_PLAYING);

        /* ---- Receive loop ---- */
        bool connection_lost = false;
        while (c->running && !connection_lost) {
            fd_set read_set;
            FD_ZERO(&read_set);
            FD_SET(c->tcp_fd, &read_set);

            struct timeval tv = { .tv_sec = 0, .tv_usec = 100000 };
            int ret = select(c->tcp_fd + 1, &read_set, NULL, NULL, &tv);
            if (ret < 0) {
                if (errno == EINTR) continue;
                break;
            }
            if (ret == 0) continue;

            int avail = RECV_BUF_SIZE - c->recv_len;
            if (avail <= 0) {
                c->recv_len = 0;
                avail = RECV_BUF_SIZE;
            }

            int n = (int)recv(c->tcp_fd, c->recv_buf + c->recv_len, avail, 0);
            if (n <= 0) {
                if (errno == EINTR || errno == EAGAIN) continue;
                fprintf(stderr, "[RtspClient] Connection lost: %s\n",
                        strerror(errno));
                notify_event(c, RTSP_CLIENT_EVENT_DISCONNECTED);
                connection_lost = true;
                continue;
            }
            c->recv_len += n;
            c->bytes_received += n;

            /* Parse interleaved frames */
            int consumed = 0;
            while (c->recv_len - consumed >= 4) {
                uint8_t* p = c->recv_buf + consumed;

                if (p[0] != 0x24) {
                    consumed++;
                    continue;
                }

                int channel = p[1];
                int frame_len = (p[2] << 8) | p[3];

                if (c->recv_len - consumed < 4 + frame_len) {
                    break;
                }

                uint8_t* frame_data = p + 4;

                if (channel == 0) {
                    if (frame_len > 0 && frame_len < MAX_PACKET_SIZE) {
                        memcpy(pkt, frame_data, frame_len);
                        pthread_mutex_lock(&c->mutex);
                        process_rtp(c, pkt, frame_len);
                        pthread_mutex_unlock(&c->mutex);
                    }
                }

                consumed += 4 + frame_len;
            }

            if (consumed > 0) {
                int remaining = c->recv_len - consumed;
                if (remaining > 0) {
                    memmove(c->recv_buf, c->recv_buf + consumed, remaining);
                }
                c->recv_len = remaining;
            }
        }

        /* Connection lost, loop back to reconnect if enabled */
        if (c->tcp_fd > 0) {
            close(c->tcp_fd);
            c->tcp_fd = -1;
        }

        pthread_mutex_lock(&c->mutex);
        if (c->state == RTSP_CLIENT_STATE_PLAYING) {
            c->state = RTSP_CLIENT_STATE_CLOSED;
        }
        pthread_mutex_unlock(&c->mutex);
    }

    /* Final flush */
    pthread_mutex_lock(&c->mutex);
    au_flush(c);
    pthread_mutex_unlock(&c->mutex);

    free(pkt);
    return NULL;
}

/* ========================================================================
 *  Helpers
 * ======================================================================== */
static void notify_event(RtspClient* c, RtspClientEvent evt)
{
    if (c->event_cb) {
        c->event_cb(c, evt, c->event_userdata);
    }
}

/* ========================================================================
 *  Public API
 * ======================================================================== */
RtspClient* RtspClient_Create(const char* url, RtspClientCodec codec,
                              RtspClientFrameCallback callback,
                              void* userdata)
{
    RtspClientConfig def;
    def.auto_reconnect      = 1;
    def.reconnect_interval_ms = DEFAULT_RECONNECT_MS;
    def.recv_timeout_ms     = DEFAULT_RECV_TIMEOUT;

    return RtspClient_CreateEx(url, codec, &def, callback, userdata,
                               NULL, NULL);
}

RtspClient* RtspClient_CreateEx(const char* url, RtspClientCodec codec,
                                const RtspClientConfig* config,
                                RtspClientFrameCallback frame_cb,
                                void* frame_userdata,
                                RtspClientEventCallback event_cb,
                                void* event_userdata)
{
    if (!url) return NULL;

    RtspClient* c = (RtspClient*)calloc(1, sizeof(RtspClient));
    if (!c) return NULL;

    strncpy(c->url, url, sizeof(c->url) - 1);
    c->codec = codec;
    c->frame_cb = frame_cb;
    c->frame_userdata = frame_userdata;
    c->event_cb = event_cb;
    c->event_userdata = event_userdata;
    c->cseq = 1;
    c->port = 554;
    c->state = RTSP_CLIENT_STATE_IDLE;
    pthread_mutex_init(&c->mutex, NULL);

    /* Apply config */
    if (config) {
        c->config = *config;
    } else {
        c->config.auto_reconnect      = 1;
        c->config.reconnect_interval_ms = DEFAULT_RECONNECT_MS;
        c->config.recv_timeout_ms     = DEFAULT_RECV_TIMEOUT;
    }

    if (parse_url(c) != 0) {
        free(c);
        return NULL;
    }

    c->au_buf   = (uint8_t*)malloc(MAX_NALU_SIZE);
    c->frag_buf = (uint8_t*)malloc(MAX_NALU_SIZE);
    c->recv_buf = (uint8_t*)malloc(RECV_BUF_SIZE);
    if (!c->au_buf || !c->frag_buf || !c->recv_buf) {
        fprintf(stderr, "[RtspClient] Failed to allocate buffers\n");
        goto fail;
    }

    printf("[RtspClient] Starting (%s, auto_reconnect=%d)\n",
           codec == RTSP_CLIENT_CODEC_H265 ? "H265" : "H264",
           c->config.auto_reconnect);

    c->running = true;
    if (pthread_create(&c->thread_id, NULL, rtsp_client_thread, c) != 0) {
        fprintf(stderr, "[RtspClient] Failed to create thread\n");
        goto fail;
    }

    return c;

fail:
    if (c->tcp_fd > 0) close(c->tcp_fd);
    free(c->au_buf);
    free(c->frag_buf);
    free(c->recv_buf);
    pthread_mutex_destroy(&c->mutex);
    free(c);
    return NULL;
}

void RtspClient_SetEventCallback(RtspClient* client,
                                 RtspClientEventCallback cb,
                                 void* userdata)
{
    if (!client) return;
    pthread_mutex_lock(&client->mutex);
    client->event_cb = cb;
    client->event_userdata = userdata;
    pthread_mutex_unlock(&client->mutex);
}

bool RtspClient_IsConnected(RtspClient* client)
{
    if (!client) return false;
    return client->state == RTSP_CLIENT_STATE_PLAYING;
}

RtspClientState RtspClient_GetState(RtspClient* client)
{
    if (!client) return RTSP_CLIENT_STATE_IDLE;
    return client->state;
}

int RtspClient_GetStats(RtspClient* client, RtspClientStats* stats)
{
    if (!client || !stats) return -1;
    pthread_mutex_lock(&client->mutex);
    stats->rtp_packets_received = client->rtp_packets_received;
    stats->frames_assembled     = client->frames_assembled;
    stats->bytes_received       = client->bytes_received;
    stats->reconnect_count      = client->reconnect_count;
    stats->last_rtp_seq         = client->last_rtp_seq;
    stats->last_rtp_ts          = client->last_rtp_ts;
    pthread_mutex_unlock(&client->mutex);
    return 0;
}

void RtspClient_Destroy(RtspClient* client)
{
    if (!client) return;

    /* Stop thread */
    client->running = false;
    if (client->thread_id != 0) {
        pthread_join(client->thread_id, NULL);
        client->thread_id = 0;
    }

    /* Send TEARDOWN if still connected */
    if (client->tcp_fd > 0 && client->session[0] != '\0') {
        rtsp_teardown(client);
    }

    /* Close socket */
    if (client->tcp_fd > 0) {
        close(client->tcp_fd);
        client->tcp_fd = -1;
    }

    /* Free buffers */
    free(client->au_buf);
    free(client->frag_buf);
    free(client->recv_buf);

    pthread_mutex_destroy(&client->mutex);

    printf("[RtspClient] Disconnected and cleaned up\n");

    free(client);
}