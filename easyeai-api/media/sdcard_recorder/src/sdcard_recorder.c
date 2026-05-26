#include "sdcard_recorder.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

void SdRecorder_EnsureDir(const char *dir)
{
    struct stat st = {0};
    if (stat(dir, &st) == -1) {
        mkdir(dir, 0755);
    }
}

void SdRecorder_GeneratePath(const char *dir, const char *prefix,
                             char *buf, size_t max_len)
{
    time_t t = time(NULL);
    struct tm *tm_info = localtime(&t);
    const char *pf = (prefix && prefix[0] != '\0') ? prefix : "record";

    snprintf(buf, max_len, "%s/%s_%04d%02d%02d_%02d%02d%02d.mp4",
             dir, pf,
             tm_info->tm_year + 1900, tm_info->tm_mon + 1, tm_info->tm_mday,
             tm_info->tm_hour, tm_info->tm_min, tm_info->tm_sec);
}

void SdRecorder_CleanOldRecords(const char *dir, int max_size_mb)
{
    DIR *d;
    struct dirent *ent;
    struct stat st;
    char path[512];
    long long total_size = 0;

    if (max_size_mb <= 0)
        return;

    d = opendir(dir);
    if (!d)
        return;

    while ((ent = readdir(d)) != NULL) {
        if (ent->d_type == DT_REG && strstr(ent->d_name, ".mp4")) {
            snprintf(path, sizeof(path), "%s/%s", dir, ent->d_name);
            if (stat(path, &st) == 0)
                total_size += st.st_size;
        }
    }
    closedir(d);

    long long max_bytes = (long long)max_size_mb * 1024 * 1024;

    while (total_size > max_bytes) {
        char oldest_file[512] = {0};
        time_t oldest_time = -1;

        d = opendir(dir);
        if (!d)
            break;
        while ((ent = readdir(d)) != NULL) {
            if (ent->d_type == DT_REG && strstr(ent->d_name, ".mp4")) {
                snprintf(path, sizeof(path), "%s/%s", dir, ent->d_name);
                if (stat(path, &st) == 0) {
                    if (oldest_time == -1 || st.st_mtime < oldest_time) {
                        oldest_time = st.st_mtime;
                        strncpy(oldest_file, path, sizeof(oldest_file) - 1);
                    }
                }
            }
        }
        closedir(d);

        if (oldest_file[0] != '\0') {
            if (stat(oldest_file, &st) == 0) {
                printf("[SdRecorder] Deleting old record: %s (%.2f MB)\n",
                       oldest_file, st.st_size / 1024.0 / 1024.0);
                total_size -= st.st_size;
                unlink(oldest_file);
            } else {
                break;
            }
        } else {
            break;
        }
    }
}
