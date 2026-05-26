#include "media_muxer.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>

#define MP4_TIMESCALE 1000000

typedef struct {
    uint32_t offset;
    uint32_t size;
    uint32_t delta;
    uint8_t  is_sync;
    uint64_t pts;
} Mp4Sample_t;

typedef struct {
    Mp4Sample_t *samples;
    int sample_count;
    int sample_capacity;
    
    uint8_t *vps; int vps_len;
    uint8_t *sps; int sps_len;
    uint8_t *pps; int pps_len;
    
    uint32_t track_id;
    uint32_t timescale;
    uint64_t duration;
    
    int width, height;
    MediaMuxerVCodec_e vcodec;
    MediaMuxerACodec_e acodec;
    int is_video;
    
    uint64_t last_pts;
} Mp4Track_t;

typedef struct {
    FILE *fd;
    uint32_t mdat_offset;
    uint32_t mdat_size;
    
    Mp4Track_t video;
    Mp4Track_t audio;
    
    bool has_video;
    bool has_audio;
    uint32_t creation_time;
} Mp4Context_t;

// --- Helper Functions for Big-Endian Writing ---
static void w8(FILE *f, uint8_t v) { fputc(v, f); }
static void w16(FILE *f, uint16_t v) { w8(f, v>>8); w8(f, v&0xFF); }
static void w24(FILE *f, uint32_t v) { w8(f, v>>16); w8(f, v>>8); w8(f, v&0xFF); }
static void w32(FILE *f, uint32_t v) { w8(f, v>>24); w8(f, v>>16); w8(f, v>>8); w8(f, v&0xFF); }
static void w_str(FILE *f, const char *s) { fwrite(s, 1, strlen(s), f); }

#define BEGIN_BOX(type) \
    uint32_t _start_##type = ftell(ctx->fd); \
    w32(ctx->fd, 0); \
    w_str(ctx->fd, #type);

#define END_BOX(type) \
    { \
        uint32_t _end = ftell(ctx->fd); \
        fseek(ctx->fd, _start_##type, SEEK_SET); \
        w32(ctx->fd, _end - _start_##type); \
        fseek(ctx->fd, _end, SEEK_SET); \
    }

// Get Mac time for MP4 creation time (seconds since 1904-01-01)
static uint32_t get_mac_time() {
    return (uint32_t)time(NULL) + 2082844800;
}

MediaMuxerHandle MediaMuxer_Create(const char* file_path, MediaMuxerFmt_e format, 
                                   MediaMuxerVideoParam_t *v_param, 
                                   MediaMuxerAudioParam_t *a_param)
{
    if (!file_path || (!v_param && !a_param)) return NULL;

    Mp4Context_t *ctx = (Mp4Context_t*)calloc(1, sizeof(Mp4Context_t));
    if (!ctx) return NULL;

    ctx->fd = fopen(file_path, "wb");
    if (!ctx->fd) {
        free(ctx);
        return NULL;
    }

    ctx->creation_time = get_mac_time();

    if (v_param) {
        ctx->has_video = true;
        ctx->video.is_video = 1;
        ctx->video.track_id = 1;
        ctx->video.timescale = MP4_TIMESCALE;
        ctx->video.width = v_param->width;
        ctx->video.height = v_param->height;
        ctx->video.vcodec = v_param->codec;
        ctx->video.sample_capacity = 1024;
        ctx->video.samples = malloc(ctx->video.sample_capacity * sizeof(Mp4Sample_t));
    }

    if (a_param) {
        ctx->has_audio = true;
        ctx->audio.is_video = 0;
        ctx->audio.track_id = ctx->has_video ? 2 : 1;
        ctx->audio.timescale = MP4_TIMESCALE;
        ctx->audio.acodec = a_param->codec;
        ctx->audio.sample_capacity = 1024;
        ctx->audio.samples = malloc(ctx->audio.sample_capacity * sizeof(Mp4Sample_t));
    }

    // Write ftyp
    BEGIN_BOX(ftyp);
        w_str(ctx->fd, "isom");
        w32(ctx->fd, 0x00000200);
        w_str(ctx->fd, "isom");
        w_str(ctx->fd, "iso2");
        w_str(ctx->fd, "avc1");
        w_str(ctx->fd, "mp41");
    END_BOX(ftyp);

    // Write mdat header (size to be filled later)
    ctx->mdat_offset = ftell(ctx->fd);
    w32(ctx->fd, 0); // size placeholder
    w_str(ctx->fd, "mdat");

    return (MediaMuxerHandle)ctx;
}

static int parse_and_write_nalus(Mp4Context_t *ctx, Mp4Track_t *track, uint8_t *data, unsigned int len) {
    int offset = 0;
    int written = 0;
    while (offset < len) {
        int sc_len = 0;
        if (offset + 3 <= len && data[offset]==0 && data[offset+1]==0 && data[offset+2]==1) sc_len = 3;
        else if (offset + 4 <= len && data[offset]==0 && data[offset+1]==0 && data[offset+2]==0 && data[offset+3]==1) sc_len = 4;
        
        if (sc_len > 0) {
            int next_offset = offset + sc_len;
            while (next_offset < len) {
                if ((next_offset + 3 <= len && data[next_offset]==0 && data[next_offset+1]==0 && data[next_offset+2]==1) ||
                    (next_offset + 4 <= len && data[next_offset]==0 && data[next_offset+1]==0 && data[next_offset+2]==0 && data[next_offset+3]==1)) {
                    break;
                }
                next_offset++;
            }
            int nalu_len = next_offset - (offset + sc_len);
            uint8_t *nalu_data = &data[offset + sc_len];
            
            bool write_to_mdat = true;
            if (track->vcodec == MEDIA_MUXER_VCODEC_H264) {
                uint8_t type = nalu_data[0] & 0x1F;
                if (type == 7) {
                    if (!track->sps) { track->sps = malloc(nalu_len); memcpy(track->sps, nalu_data, nalu_len); track->sps_len = nalu_len; }
                    write_to_mdat = false;
                } else if (type == 8) {
                    if (!track->pps) { track->pps = malloc(nalu_len); memcpy(track->pps, nalu_data, nalu_len); track->pps_len = nalu_len; }
                    write_to_mdat = false;
                }
            } else if (track->vcodec == MEDIA_MUXER_VCODEC_H265) {
                uint8_t type = (nalu_data[0] >> 1) & 0x3F;
                if (type == 32) {
                    if (!track->vps) { track->vps = malloc(nalu_len); memcpy(track->vps, nalu_data, nalu_len); track->vps_len = nalu_len; }
                    write_to_mdat = false;
                } else if (type == 33) {
                    if (!track->sps) { track->sps = malloc(nalu_len); memcpy(track->sps, nalu_data, nalu_len); track->sps_len = nalu_len; }
                    write_to_mdat = false;
                } else if (type == 34) {
                    if (!track->pps) { track->pps = malloc(nalu_len); memcpy(track->pps, nalu_data, nalu_len); track->pps_len = nalu_len; }
                    write_to_mdat = false;
                }
            }
            
            if (write_to_mdat) {
                w32(ctx->fd, nalu_len);
                fwrite(nalu_data, 1, nalu_len, ctx->fd);
                written += 4 + nalu_len;
            }
            offset = next_offset;
        } else {
            offset++;
        }
    }
    return written;
}

int MediaMuxer_PushVideo(MediaMuxerHandle handle, uint8_t *data, unsigned int len, 
                         uint64_t pts_us, int is_keyframe)
{
    Mp4Context_t *ctx = (Mp4Context_t*)handle;
    if (!ctx || !ctx->has_video || !data) return -1;
    Mp4Track_t *track = &ctx->video;

    if (track->sample_count > 0) {
        track->samples[track->sample_count - 1].delta = pts_us > track->last_pts ? (pts_us - track->last_pts) : 0;
        track->duration += track->samples[track->sample_count - 1].delta;
    }

    if (track->sample_count >= track->sample_capacity) {
        track->sample_capacity *= 2;
        track->samples = realloc(track->samples, track->sample_capacity * sizeof(Mp4Sample_t));
    }

    Mp4Sample_t *sample = &track->samples[track->sample_count++];
    sample->offset = ftell(ctx->fd);
    sample->pts = pts_us;
    sample->is_sync = is_keyframe;
    sample->delta = 33333; // Default guess (30fps), will be corrected by next frame

    int written = parse_and_write_nalus(ctx, track, data, len);
    sample->size = written;
    track->last_pts = pts_us;

    ctx->mdat_size += written;
    return 0;
}

int MediaMuxer_PushAudio(MediaMuxerHandle handle, uint8_t *data, unsigned int len, 
                         uint64_t pts_us)
{
    Mp4Context_t *ctx = (Mp4Context_t*)handle;
    if (!ctx || !ctx->has_audio || !data) return -1;
    Mp4Track_t *track = &ctx->audio;

    if (track->sample_count > 0) {
        track->samples[track->sample_count - 1].delta = pts_us > track->last_pts ? (pts_us - track->last_pts) : 0;
        track->duration += track->samples[track->sample_count - 1].delta;
    }

    if (track->sample_count >= track->sample_capacity) {
        track->sample_capacity *= 2;
        track->samples = realloc(track->samples, track->sample_capacity * sizeof(Mp4Sample_t));
    }

    Mp4Sample_t *sample = &track->samples[track->sample_count++];
    sample->offset = ftell(ctx->fd);
    sample->pts = pts_us;
    sample->is_sync = 1; // Audio is usually always sync
    sample->delta = 20000; // Guess

    fwrite(data, 1, len, ctx->fd);
    sample->size = len;
    track->last_pts = pts_us;
    ctx->mdat_size += len;
    return 0;
}

static void write_stts(Mp4Context_t *ctx, Mp4Track_t *track) {
    BEGIN_BOX(stts);
    w8(ctx->fd, 0); w24(ctx->fd, 0); // version & flags
    
    int entry_count = 0;
    for(int i=0; i<track->sample_count; ) {
        int j = i + 1;
        while(j < track->sample_count && track->samples[j].delta == track->samples[i].delta) j++;
        entry_count++; i = j;
    }
    
    w32(ctx->fd, entry_count);
    for(int i=0; i<track->sample_count; ) {
        int j = i + 1;
        while(j < track->sample_count && track->samples[j].delta == track->samples[i].delta) j++;
        w32(ctx->fd, j - i);
        w32(ctx->fd, track->samples[i].delta);
        i = j;
    }
    END_BOX(stts);
}

static void write_track(Mp4Context_t *ctx, Mp4Track_t *track) {
    BEGIN_BOX(trak);
        BEGIN_BOX(tkhd);
            w8(ctx->fd, 0); w24(ctx->fd, 3); // flags: enabled, in_movie
            w32(ctx->fd, ctx->creation_time); w32(ctx->fd, ctx->creation_time);
            w32(ctx->fd, track->track_id); w32(ctx->fd, 0);
            w32(ctx->fd, track->duration);
            for(int i=0;i<2;i++) w32(ctx->fd, 0); // reserved
            w16(ctx->fd, 0); w16(ctx->fd, track->is_video ? 0 : 0x0100); // layer/alt/volume
            w16(ctx->fd, 0);
            // matrix
            w32(ctx->fd, 0x00010000); w32(ctx->fd, 0); w32(ctx->fd, 0);
            w32(ctx->fd, 0); w32(ctx->fd, 0x00010000); w32(ctx->fd, 0);
            w32(ctx->fd, 0); w32(ctx->fd, 0); w32(ctx->fd, 0x40000000);
            w32(ctx->fd, track->width << 16); w32(ctx->fd, track->height << 16);
        END_BOX(tkhd);
        
        BEGIN_BOX(mdia);
            BEGIN_BOX(mdhd);
                w8(ctx->fd, 0); w24(ctx->fd, 0);
                w32(ctx->fd, ctx->creation_time); w32(ctx->fd, ctx->creation_time);
                w32(ctx->fd, track->timescale); w32(ctx->fd, track->duration);
                w16(ctx->fd, 0x55C4); w16(ctx->fd, 0); // language, pre_defined
            END_BOX(mdhd);
            
            BEGIN_BOX(hdlr);
                w8(ctx->fd, 0); w24(ctx->fd, 0); w32(ctx->fd, 0);
                w_str(ctx->fd, track->is_video ? "vide" : "soun");
                w32(ctx->fd, 0); w32(ctx->fd, 0); w32(ctx->fd, 0);
                w_str(ctx->fd, track->is_video ? "VideoHandler" : "AudioHandler"); w8(ctx->fd, 0);
            END_BOX(hdlr);
            
            BEGIN_BOX(minf);
                if (track->is_video) {
                    BEGIN_BOX(vmhd);
                    w8(ctx->fd, 0); w24(ctx->fd, 1);
                    w16(ctx->fd, 0); w16(ctx->fd, 0); w16(ctx->fd, 0); w16(ctx->fd, 0);
                    END_BOX(vmhd);
                } else {
                    BEGIN_BOX(smhd);
                    w8(ctx->fd, 0); w24(ctx->fd, 0);
                    w16(ctx->fd, 0); w16(ctx->fd, 0);
                    END_BOX(smhd);
                }
                
                BEGIN_BOX(dinf);
                    BEGIN_BOX(dref);
                    w8(ctx->fd, 0); w24(ctx->fd, 0); w32(ctx->fd, 1);
                    BEGIN_BOX(url );
                    w8(ctx->fd, 0); w24(ctx->fd, 1);
                    END_BOX(url );
                    END_BOX(dref);
                END_BOX(dinf);
                
                BEGIN_BOX(stbl);
                    BEGIN_BOX(stsd);
                        w8(ctx->fd, 0); w24(ctx->fd, 0); w32(ctx->fd, 1); // entry count
                        if (track->is_video) {
                            if (track->vcodec == MEDIA_MUXER_VCODEC_H265) {
                                BEGIN_BOX(hev1);
                                for(int i=0;i<6;i++) w8(ctx->fd, 0); w16(ctx->fd, 1); // dref index
                                w16(ctx->fd, 0); w16(ctx->fd, 0); w32(ctx->fd, 0); w32(ctx->fd, 0); w32(ctx->fd, 0);
                                w16(ctx->fd, track->width); w16(ctx->fd, track->height);
                                w32(ctx->fd, 0x00480000); w32(ctx->fd, 0x00480000); w32(ctx->fd, 0); w16(ctx->fd, 1);
                                for(int i=0;i<32;i++) w8(ctx->fd, 0); w16(ctx->fd, 0x0018); w16(ctx->fd, -1);
                                
                                if (track->vps && track->sps && track->pps) {
                                    BEGIN_BOX(hvcC);
                                    w8(ctx->fd, 1); w8(ctx->fd, 1); w32(ctx->fd, 0); w32(ctx->fd, 0); w16(ctx->fd, 0);
                                    w16(ctx->fd, 0xF000); w8(ctx->fd, 0xFC); w8(ctx->fd, 0xFC); w8(ctx->fd, 0xF8); w8(ctx->fd, 0xF8);
                                    w16(ctx->fd, 0); w8(ctx->fd, 0x0F);
                                    w8(ctx->fd, 3); // num arrays
                                    w8(ctx->fd, 32 | 0x80); w16(ctx->fd, 1); w16(ctx->fd, track->vps_len); fwrite(track->vps, 1, track->vps_len, ctx->fd);
                                    w8(ctx->fd, 33 | 0x80); w16(ctx->fd, 1); w16(ctx->fd, track->sps_len); fwrite(track->sps, 1, track->sps_len, ctx->fd);
                                    w8(ctx->fd, 34 | 0x80); w16(ctx->fd, 1); w16(ctx->fd, track->pps_len); fwrite(track->pps, 1, track->pps_len, ctx->fd);
                                    END_BOX(hvcC);
                                }
                                END_BOX(hev1);
                            } else {
                                BEGIN_BOX(avc1);
                                for(int i=0;i<6;i++) w8(ctx->fd, 0); w16(ctx->fd, 1); // dref index
                                w16(ctx->fd, 0); w16(ctx->fd, 0); w32(ctx->fd, 0); w32(ctx->fd, 0); w32(ctx->fd, 0);
                                w16(ctx->fd, track->width); w16(ctx->fd, track->height);
                                w32(ctx->fd, 0x00480000); w32(ctx->fd, 0x00480000); w32(ctx->fd, 0); w16(ctx->fd, 1);
                                for(int i=0;i<32;i++) w8(ctx->fd, 0); w16(ctx->fd, 0x0018); w16(ctx->fd, -1);
                                
                                if (track->sps && track->pps) {
                                    BEGIN_BOX(avcC);
                                    w8(ctx->fd, 1); w8(ctx->fd, track->sps[1]); w8(ctx->fd, track->sps[2]); w8(ctx->fd, track->sps[3]);
                                    w8(ctx->fd, 0xFF); w8(ctx->fd, 0xE1);
                                    w16(ctx->fd, track->sps_len); fwrite(track->sps, 1, track->sps_len, ctx->fd);
                                    w8(ctx->fd, 1); w16(ctx->fd, track->pps_len); fwrite(track->pps, 1, track->pps_len, ctx->fd);
                                    END_BOX(avcC);
                                }
                                END_BOX(avc1);
                            }
                        } else {
                            BEGIN_BOX(mp4a); // basic generic audio stub
                            for (int i = 0; i < 6; i++) w8(ctx->fd, 0);
                            w16(ctx->fd, 1);
                            w32(ctx->fd, 0); w32(ctx->fd, 0); w16(ctx->fd, 2); w16(ctx->fd, 16); w16(ctx->fd, 0); w16(ctx->fd, 0);
                            w32(ctx->fd, 8000 << 16); // default 8k
                            END_BOX(mp4a);
                        }
                    END_BOX(stsd);
                    
                    write_stts(ctx, track);
                    
                    if (track->is_video) {
                        BEGIN_BOX(stss);
                        w8(ctx->fd, 0); w24(ctx->fd, 0);
                        int sync_cnt = 0;
                        for(int i=0; i<track->sample_count; i++) if (track->samples[i].is_sync) sync_cnt++;
                        w32(ctx->fd, sync_cnt);
                        for(int i=0; i<track->sample_count; i++) if (track->samples[i].is_sync) w32(ctx->fd, i + 1);
                        END_BOX(stss);
                    }
                    
                    BEGIN_BOX(stsc);
                    w8(ctx->fd, 0); w24(ctx->fd, 0); w32(ctx->fd, 1);
                    w32(ctx->fd, 1); w32(ctx->fd, 1); w32(ctx->fd, 1); // 1 sample per chunk
                    END_BOX(stsc);
                    
                    BEGIN_BOX(stsz);
                    w8(ctx->fd, 0); w24(ctx->fd, 0); w32(ctx->fd, 0);
                    w32(ctx->fd, track->sample_count);
                    for(int i=0; i<track->sample_count; i++) w32(ctx->fd, track->samples[i].size);
                    END_BOX(stsz);
                    
                    BEGIN_BOX(stco);
                    w8(ctx->fd, 0); w24(ctx->fd, 0); w32(ctx->fd, track->sample_count);
                    for(int i=0; i<track->sample_count; i++) w32(ctx->fd, track->samples[i].offset);
                    END_BOX(stco);
                END_BOX(stbl);
            END_BOX(minf);
        END_BOX(mdia);
    END_BOX(trak);
}

void MediaMuxer_Destroy(MediaMuxerHandle handle)
{
    Mp4Context_t *ctx = (Mp4Context_t*)handle;
    if (!ctx) return;

    if (ctx->video.sample_count > 0) ctx->video.duration += ctx->video.samples[ctx->video.sample_count - 1].delta;
    if (ctx->audio.sample_count > 0) ctx->audio.duration += ctx->audio.samples[ctx->audio.sample_count - 1].delta;

    uint32_t max_duration = ctx->has_video ? ctx->video.duration : 0;
    if (ctx->has_audio && ctx->audio.duration > max_duration) max_duration = ctx->audio.duration;

    fseek(ctx->fd, ctx->mdat_offset, SEEK_SET);
    w32(ctx->fd, ctx->mdat_size + 8); // mdat box size
    fseek(ctx->fd, 0, SEEK_END);

    BEGIN_BOX(moov);
        BEGIN_BOX(mvhd);
            w8(ctx->fd, 0); w24(ctx->fd, 0); // version & flags
            w32(ctx->fd, ctx->creation_time); w32(ctx->fd, ctx->creation_time);
            w32(ctx->fd, MP4_TIMESCALE);
            w32(ctx->fd, max_duration);
            w32(ctx->fd, 0x00010000); w16(ctx->fd, 0x0100); w16(ctx->fd, 0);
            w32(ctx->fd, 0); w32(ctx->fd, 0);
            w32(ctx->fd, 0x00010000); w32(ctx->fd, 0); w32(ctx->fd, 0);
            w32(ctx->fd, 0); w32(ctx->fd, 0x00010000); w32(ctx->fd, 0);
            w32(ctx->fd, 0); w32(ctx->fd, 0); w32(ctx->fd, 0x40000000);
            for(int i=0;i<6;i++) w32(ctx->fd, 0); // pre_defined
            w32(ctx->fd, 3); // next track id
        END_BOX(mvhd);
        
        if (ctx->has_video) write_track(ctx, &ctx->video);
        if (ctx->has_audio) write_track(ctx, &ctx->audio);
    END_BOX(moov);

    fclose(ctx->fd);

    if (ctx->has_video) {
        if(ctx->video.samples) free(ctx->video.samples);
        if(ctx->video.vps) free(ctx->video.vps);
        if(ctx->video.sps) free(ctx->video.sps);
        if(ctx->video.pps) free(ctx->video.pps);
    }
    if (ctx->has_audio) {
        if(ctx->audio.samples) free(ctx->audio.samples);
    }
    free(ctx);
}
