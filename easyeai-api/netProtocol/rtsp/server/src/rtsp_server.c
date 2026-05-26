#include "rtsp_server.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/select.h>
#include <time.h>

#define MAX_CLIENTS 5
#define MAX_PAYLOAD 1400

typedef struct {
    int fd;
    int state; // 0: closed, 1: init, 2: ready, 3: playing
    char ip[64];
    bool is_tcp;
    int tcp_rtp_channel;
    int tcp_rtcp_channel;
    int client_rtp_port;
    int client_rtcp_port;
    int server_rtp_fd;
    int server_rtcp_fd;
    int server_rtp_port;
    int server_rtcp_port;
    uint32_t session_id;
} RtspClient;

struct RtspServer {
    int port;
    char path[64];
    RtspServerCodec codec;
    int listen_fd;
    pthread_t thread_id;
    bool running;
    RtspClient clients[MAX_CLIENTS];
    pthread_mutex_t mutex;
    uint16_t rtp_seq;
    uint32_t rtp_ssrc;
};

static void close_client(RtspClient* client) {
    if (client->fd > 0) close(client->fd);
    if (client->server_rtp_fd > 0) close(client->server_rtp_fd);
    if (client->server_rtcp_fd > 0) close(client->server_rtcp_fd);
    memset(client, 0, sizeof(RtspClient));
}

static void handle_rtsp_request(RtspServer* server, int client_idx, const char* req) {
    RtspClient* client = &server->clients[client_idx];
    char method[32] = {0};
    char url[256] = {0};
    char version[32] = {0};
    sscanf(req, "%31s %255s %31s", method, url, version);

    int cseq = 0;
    char* cseq_ptr = strstr(req, "CSeq:");
    if (cseq_ptr) sscanf(cseq_ptr, "CSeq: %d", &cseq);

    char resp[1024];
    if (strcmp(method, "OPTIONS") == 0) {
        snprintf(resp, sizeof(resp),
            "RTSP/1.0 200 OK\r\n"
            "CSeq: %d\r\n"
            "Public: OPTIONS, DESCRIBE, SETUP, PLAY, TEARDOWN\r\n"
            "\r\n", cseq);
        send(client->fd, resp, strlen(resp), 0);
    } else if (strcmp(method, "DESCRIBE") == 0) {
        char sdp[512];
        snprintf(sdp, sizeof(sdp),
            "v=0\r\n"
            "o=- %u 1 IN IP4 0.0.0.0\r\n"
            "t=0 0\r\n"
            "a=control:*\r\n"
            "m=video 0 RTP/AVP 96\r\n"
            "a=rtpmap:96 %s/90000\r\n"
            "a=control:track0\r\n",
            (unsigned int)time(NULL),
            server->codec == RTSP_SERVER_CODEC_H265 ? "H265" : "H264"
        );
        snprintf(resp, sizeof(resp),
            "RTSP/1.0 200 OK\r\n"
            "CSeq: %d\r\n"
            "Content-Base: %s/\r\n"
            "Content-Type: application/sdp\r\n"
            "Content-Length: %zu\r\n"
            "\r\n"
            "%s", cseq, url, strlen(sdp), sdp);
        send(client->fd, resp, strlen(resp), 0);
    } else if (strcmp(method, "SETUP") == 0) {
        char* trans_ptr = strstr(req, "Transport:");
        if (trans_ptr) {
            if (strstr(trans_ptr, "TCP") != NULL || strstr(trans_ptr, "interleaved") != NULL) {
                client->is_tcp = true;
                char* inter_ptr = strstr(trans_ptr, "interleaved=");
                if (inter_ptr) {
                    sscanf(inter_ptr, "interleaved=%d-%d", &client->tcp_rtp_channel, &client->tcp_rtcp_channel);
                } else {
                    client->tcp_rtp_channel = 0;
                    client->tcp_rtcp_channel = 1;
                }
            } else {
                client->is_tcp = false;
                char* port_ptr = strstr(trans_ptr, "client_port=");
                if (port_ptr) {
                    sscanf(port_ptr, "client_port=%d-%d", &client->client_rtp_port, &client->client_rtcp_port);
                }
            }
        }
        
        client->session_id = (uint32_t)rand();
        if (client->session_id == 0) client->session_id = 1;
        client->state = 2; // READY

        if (client->is_tcp) {
            snprintf(resp, sizeof(resp),
                "RTSP/1.0 200 OK\r\n"
                "CSeq: %d\r\n"
                "Transport: RTP/AVP/TCP;unicast;interleaved=%d-%d\r\n"
                "Session: %u\r\n"
                "\r\n", cseq, client->tcp_rtp_channel, client->tcp_rtcp_channel, client->session_id);
        } else {
            client->server_rtp_fd = socket(AF_INET, SOCK_DGRAM, 0);
            struct sockaddr_in saddr;
            memset(&saddr, 0, sizeof(saddr));
            saddr.sin_family = AF_INET;
            saddr.sin_addr.s_addr = INADDR_ANY;
            saddr.sin_port = 0;
            bind(client->server_rtp_fd, (struct sockaddr*)&saddr, sizeof(saddr));
            socklen_t slen = sizeof(saddr);
            getsockname(client->server_rtp_fd, (struct sockaddr*)&saddr, &slen);
            client->server_rtp_port = ntohs(saddr.sin_port);

            client->server_rtcp_fd = socket(AF_INET, SOCK_DGRAM, 0);
            memset(&saddr, 0, sizeof(saddr));
            saddr.sin_family = AF_INET;
            saddr.sin_addr.s_addr = INADDR_ANY;
            saddr.sin_port = 0;
            bind(client->server_rtcp_fd, (struct sockaddr*)&saddr, sizeof(saddr));
            getsockname(client->server_rtcp_fd, (struct sockaddr*)&saddr, &slen);
            client->server_rtcp_port = ntohs(saddr.sin_port);

            snprintf(resp, sizeof(resp),
                "RTSP/1.0 200 OK\r\n"
                "CSeq: %d\r\n"
                "Transport: RTP/AVP;unicast;client_port=%d-%d;server_port=%d-%d\r\n"
                "Session: %u\r\n"
                "\r\n", cseq, client->client_rtp_port, client->client_rtcp_port,
                client->server_rtp_port, client->server_rtcp_port, client->session_id);
        }
        send(client->fd, resp, strlen(resp), 0);
    } else if (strcmp(method, "PLAY") == 0) {
        client->state = 3; // PLAYING
        snprintf(resp, sizeof(resp),
            "RTSP/1.0 200 OK\r\n"
            "CSeq: %d\r\n"
            "Range: npt=0.000-\r\n"
            "Session: %u\r\n"
            "\r\n", cseq, client->session_id);
        send(client->fd, resp, strlen(resp), 0);
    } else if (strcmp(method, "TEARDOWN") == 0) {
        snprintf(resp, sizeof(resp),
            "RTSP/1.0 200 OK\r\n"
            "CSeq: %d\r\n"
            "\r\n", cseq);
        send(client->fd, resp, strlen(resp), 0);
        client->state = 0;
    } else {
        // Unknown or unsupported method
        snprintf(resp, sizeof(resp),
            "RTSP/1.0 501 Not Implemented\r\n"
            "CSeq: %d\r\n"
            "\r\n", cseq);
        send(client->fd, resp, strlen(resp), 0);
    }
}

static void* rtsp_server_thread(void* arg) {
    RtspServer* server = (RtspServer*)arg;
    
    while (server->running) {
        fd_set read_set;
        FD_ZERO(&read_set);
        FD_SET(server->listen_fd, &read_set);
        int max_fd = server->listen_fd;

        pthread_mutex_lock(&server->mutex);
        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (server->clients[i].state != 0 && server->clients[i].fd > 0) {
                FD_SET(server->clients[i].fd, &read_set);
                if (server->clients[i].fd > max_fd) max_fd = server->clients[i].fd;
            }
        }
        pthread_mutex_unlock(&server->mutex);

        struct timeval tv = {0, 100000}; // 100ms
        int ret = select(max_fd + 1, &read_set, NULL, NULL, &tv);
        if (ret < 0) break;
        if (ret == 0) continue;

        if (FD_ISSET(server->listen_fd, &read_set)) {
            struct sockaddr_in caddr;
            socklen_t clen = sizeof(caddr);
            int client_fd = accept(server->listen_fd, (struct sockaddr*)&caddr, &clen);
            if (client_fd > 0) {
                pthread_mutex_lock(&server->mutex);
                int added = 0;
                for (int i = 0; i < MAX_CLIENTS; i++) {
                    if (server->clients[i].state == 0) {
                        server->clients[i].fd = client_fd;
                        server->clients[i].state = 1;
                        inet_ntop(AF_INET, &caddr.sin_addr, server->clients[i].ip, sizeof(server->clients[i].ip));
                        added = 1;
                        break;
                    }
                }
                pthread_mutex_unlock(&server->mutex);
                if (!added) close(client_fd);
            }
        }

        pthread_mutex_lock(&server->mutex);
        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (server->clients[i].state != 0 && FD_ISSET(server->clients[i].fd, &read_set)) {
                char req_buf[2048] = {0};
                int len = recv(server->clients[i].fd, req_buf, sizeof(req_buf) - 1, MSG_DONTWAIT);
                if (len <= 0) {
                    close_client(&server->clients[i]);
                } else {
                    handle_rtsp_request(server, i, req_buf);
                    if (server->clients[i].state == 0) {
                        close_client(&server->clients[i]);
                    }
                }
            }
        }
        pthread_mutex_unlock(&server->mutex);
    }
    return NULL;
}

RtspServer* RtspServer_Create(int port, const char* path, RtspServerCodec codec) {
    if (path == NULL) return NULL;

    RtspServer* server = (RtspServer*)calloc(1, sizeof(RtspServer));
    if (!server) return NULL;

    server->port = port;
    snprintf(server->path, sizeof(server->path), "%s", path);
    server->codec = codec;
    srand(time(NULL));
    server->rtp_ssrc = (uint32_t)rand();
    pthread_mutex_init(&server->mutex, NULL);

    server->listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server->listen_fd < 0) {
        free(server);
        return NULL;
    }

    int opt = 1;
    setsockopt(server->listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in saddr;
    memset(&saddr, 0, sizeof(saddr));
    saddr.sin_family = AF_INET;
    saddr.sin_addr.s_addr = INADDR_ANY;
    saddr.sin_port = htons(port);

    if (bind(server->listen_fd, (struct sockaddr*)&saddr, sizeof(saddr)) < 0) {
        close(server->listen_fd);
        free(server);
        return NULL;
    }

    if (listen(server->listen_fd, 5) < 0) {
        close(server->listen_fd);
        free(server);
        return NULL;
    }

    server->running = true;
    if (pthread_create(&server->thread_id, NULL, rtsp_server_thread, server) != 0) {
        close(server->listen_fd);
        free(server);
        return NULL;
    }

    printf("[RtspServer] Hand-rolled RTSP Server started on rtsp://0.0.0.0:%d%s (%s)\n", 
           port, path, (codec == RTSP_SERVER_CODEC_H265) ? "H265" : "H264");

    return server;
}

static void send_rtp_packet(RtspServer* server, uint8_t* rtp_pkt, int len, int is_marker) {
    rtp_pkt[1] = (is_marker ? 0x80 : 0) | 96; // Payload type 96
    rtp_pkt[2] = server->rtp_seq >> 8;
    rtp_pkt[3] = server->rtp_seq & 0xFF;
    server->rtp_seq++;

    pthread_mutex_lock(&server->mutex);
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (server->clients[i].state == 3) {
            if (server->clients[i].is_tcp) {
                if (server->clients[i].fd > 0) {
                    // RTSP interleaved frame header
                    uint8_t header[4];
                    header[0] = 0x24; // '$' magic number
                    header[1] = server->clients[i].tcp_rtp_channel;
                    header[2] = (len >> 8) & 0xFF;
                    header[3] = len & 0xFF;
                    
                    // Send over the same RTSP TCP connection
                    // Using MSG_NOSIGNAL to prevent SIGPIPE when client disconnects
                    send(server->clients[i].fd, header, 4, MSG_NOSIGNAL);
                    send(server->clients[i].fd, rtp_pkt, len, MSG_NOSIGNAL);
                }
            } else {
                if (server->clients[i].server_rtp_fd > 0) {
                    struct sockaddr_in caddr;
                    memset(&caddr, 0, sizeof(caddr));
                    caddr.sin_family = AF_INET;
                    caddr.sin_port = htons(server->clients[i].client_rtp_port);
                    inet_pton(AF_INET, server->clients[i].ip, &caddr.sin_addr);
                    sendto(server->clients[i].server_rtp_fd, rtp_pkt, len, 0, (struct sockaddr*)&caddr, sizeof(caddr));
                }
            }
        }
    }
    pthread_mutex_unlock(&server->mutex);
}

static void send_nalu_h264(RtspServer* server, const uint8_t* nalu_data, int nalu_len, uint32_t ts, int is_last) {
    uint8_t rtp_buf[1500];
    rtp_buf[0] = 0x80; // V=2, P=0, X=0, CC=0
    rtp_buf[4] = (ts >> 24) & 0xFF; rtp_buf[5] = (ts >> 16) & 0xFF; rtp_buf[6] = (ts >> 8) & 0xFF; rtp_buf[7] = ts & 0xFF;
    rtp_buf[8] = (server->rtp_ssrc >> 24) & 0xFF; rtp_buf[9] = (server->rtp_ssrc >> 16) & 0xFF; rtp_buf[10] = (server->rtp_ssrc >> 8) & 0xFF; rtp_buf[11] = server->rtp_ssrc & 0xFF;

    if (nalu_len <= MAX_PAYLOAD) {
        memcpy(rtp_buf + 12, nalu_data, nalu_len);
        send_rtp_packet(server, rtp_buf, 12 + nalu_len, is_last);
    } else {
        uint8_t nalu_header = nalu_data[0];
        uint8_t fu_indicator = (nalu_header & 0xE0) | 28;
        uint8_t fu_header = (nalu_header & 0x1F) | 0x80; // Start bit set
        
        int data_offset = 1;
        while (data_offset < nalu_len) {
            int frag_len = nalu_len - data_offset;
            if (frag_len > MAX_PAYLOAD) frag_len = MAX_PAYLOAD;
            int is_frag_last = (data_offset + frag_len == nalu_len);
            
            if (is_frag_last) {
                fu_header |= 0x40; // End bit set
            }

            rtp_buf[12] = fu_indicator;
            rtp_buf[13] = fu_header;
            memcpy(rtp_buf + 14, nalu_data + data_offset, frag_len);
            
            int marker = is_frag_last ? is_last : 0;
            send_rtp_packet(server, rtp_buf, 14 + frag_len, marker);
            
            data_offset += frag_len;
            fu_header &= ~0x80; // Clear start bit
        }
    }
}

static void send_nalu_h265(RtspServer* server, const uint8_t* nalu_data, int nalu_len, uint32_t ts, int is_last) {
    uint8_t rtp_buf[1500];
    rtp_buf[0] = 0x80; // V=2
    rtp_buf[4] = (ts >> 24) & 0xFF; rtp_buf[5] = (ts >> 16) & 0xFF; rtp_buf[6] = (ts >> 8) & 0xFF; rtp_buf[7] = ts & 0xFF;
    rtp_buf[8] = (server->rtp_ssrc >> 24) & 0xFF; rtp_buf[9] = (server->rtp_ssrc >> 16) & 0xFF; rtp_buf[10] = (server->rtp_ssrc >> 8) & 0xFF; rtp_buf[11] = server->rtp_ssrc & 0xFF;

    if (nalu_len <= MAX_PAYLOAD) {
        memcpy(rtp_buf + 12, nalu_data, nalu_len);
        send_rtp_packet(server, rtp_buf, 12 + nalu_len, is_last);
    } else {
        uint8_t nal_type = (nalu_data[0] & 0x7E) >> 1;
        uint8_t payload_hdr_0 = (nalu_data[0] & 0x81) | (49 << 1); // FU type is 49
        uint8_t payload_hdr_1 = nalu_data[1];
        uint8_t fu_header = 0x80 | nal_type; // Start bit set
        
        int data_offset = 2; // skip original 2-byte NAL header
        while (data_offset < nalu_len) {
            int frag_len = nalu_len - data_offset;
            if (frag_len > MAX_PAYLOAD) frag_len = MAX_PAYLOAD;
            int is_frag_last = (data_offset + frag_len == nalu_len);
            
            if (is_frag_last) {
                fu_header |= 0x40; // End bit set
            }

            rtp_buf[12] = payload_hdr_0;
            rtp_buf[13] = payload_hdr_1;
            rtp_buf[14] = fu_header;
            memcpy(rtp_buf + 15, nalu_data + data_offset, frag_len);
            
            int marker = is_frag_last ? is_last : 0;
            send_rtp_packet(server, rtp_buf, 15 + frag_len, marker);
            
            data_offset += frag_len;
            fu_header &= ~0x80; // Clear start bit
        }
    }
}

static const uint8_t* find_start_code(const uint8_t* data, int len, int* start_code_len) {
    for (int i = 0; i < len - 3; i++) {
        if (data[i] == 0 && data[i+1] == 0 && data[i+2] == 1) {
            *start_code_len = 3;
            return &data[i];
        }
        if (data[i] == 0 && data[i+1] == 0 && data[i+2] == 0 && data[i+3] == 1) {
            *start_code_len = 4;
            return &data[i];
        }
    }
    return NULL;
}

int RtspServer_PushFrame(RtspServer* server, const uint8_t* frame, int size, uint64_t timestamp) {
    if (!server || !frame || size <= 0) return -1;

    uint32_t rtp_ts = (uint32_t)(timestamp * 90 / 1000);

    int offset = 0;
    while (offset < size) {
        int sc_len = 0;
        const uint8_t* sc_ptr = find_start_code(frame + offset, size - offset, &sc_len);
        if (!sc_ptr) {
            if (offset == 0) {
                // No start code found, assume whole frame is one NALU
                if (server->codec == RTSP_SERVER_CODEC_H265) {
                    send_nalu_h265(server, frame, size, rtp_ts, 1);
                } else {
                    send_nalu_h264(server, frame, size, rtp_ts, 1);
                }
            }
            break;
        }

        const uint8_t* nalu_start = sc_ptr + sc_len;
        int nalu_start_offset = nalu_start - frame;
        
        int next_sc_len = 0;
        const uint8_t* next_sc_ptr = find_start_code(nalu_start, size - nalu_start_offset, &next_sc_len);
        
        int nalu_len = next_sc_ptr ? (next_sc_ptr - nalu_start) : (size - nalu_start_offset);
        int is_last_nalu = (next_sc_ptr == NULL);

        if (nalu_len > 0) {
            if (server->codec == RTSP_SERVER_CODEC_H265) {
                send_nalu_h265(server, nalu_start, nalu_len, rtp_ts, is_last_nalu);
            } else {
                send_nalu_h264(server, nalu_start, nalu_len, rtp_ts, is_last_nalu);
            }
        }

        offset = next_sc_ptr ? (next_sc_ptr - frame) : size;
    }

    return 0;
}

void RtspServer_Destroy(RtspServer* server) {
    if (!server) return;
    server->running = false;
    if (server->thread_id != 0) {
        pthread_join(server->thread_id, NULL);
    }
    if (server->listen_fd > 0) close(server->listen_fd);
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (server->clients[i].state != 0) close_client(&server->clients[i]);
    }
    pthread_mutex_destroy(&server->mutex);
    free(server);
}
