#pragma once
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef enum { JOB_IDLE,JOB_PRINTING,JOB_PAUSED,JOB_COMPLETE,JOB_CANCELLED,JOB_ERROR,JOB_OFFLINE } JobState;
typedef struct {
    JobState state;
    char filename[4096];
    uint64_t file_position,file_size;
} PrintProgress;
typedef struct ProgressClient ProgressClient;
int progress_parse(const char *json,size_t length,PrintProgress *out);
ProgressClient *progress_create(void);
/* 1 means pending; 0 is a complete snapshot; -1 is a failed request. */
int progress_poll(ProgressClient *client,PrintProgress *out);
void progress_cancel(ProgressClient *client);
void progress_destroy(ProgressClient *client);
#ifdef __cplusplus
}
#endif
