#include <pthread.h>
#include <string.h>
#include "loader.h"

/* Room for a comic's worth of look-ahead and a screen's worth of art. */
#define LOADER_JOBS 256

typedef enum { JOB_FREE = 0, JOB_QUEUED, JOB_WORKING, JOB_DONE } JobState;

typedef struct {
    JobState state;
    int32_t  key;
    char     path[1024];
    Image    image;
    uint64_t order;             /* first come, first decoded */
} Job;

static Job jobs[LOADER_JOBS];
static uint64_t next_order;
static pthread_t worker;
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t wake = PTHREAD_COND_INITIALIZER;
static pthread_cond_t finished = PTHREAD_COND_INITIALIZER;
static int running;

static Job *find(int32_t key)
{
    for (int i = 0; i < LOADER_JOBS; i++)
        if (jobs[i].state != JOB_FREE && jobs[i].key == key)
            return &jobs[i];
    return NULL;
}

static Job *oldest_queued(void)
{
    Job *pick = NULL;
    for (int i = 0; i < LOADER_JOBS; i++)
        if (jobs[i].state == JOB_QUEUED
            && (!pick || jobs[i].order < pick->order))
            pick = &jobs[i];
    return pick;
}

static void *work(void *unused)
{
    (void)unused;
    pthread_mutex_lock(&lock);
    while (running) {
        Job *job = oldest_queued();
        if (!job) {
            pthread_cond_wait(&wake, &lock);
            continue;
        }
        job->state = JOB_WORKING;
        char path[sizeof(job->path)];
        memcpy(path, job->path, sizeof(path));
        pthread_mutex_unlock(&lock);
        /* stb_image keeps no state between calls, so this is safe to run
           beside the main thread; it touches nothing on the card. */
        Image image = LoadImage(path);
        pthread_mutex_lock(&lock);
        job->image = image;
        job->state = JOB_DONE;
        pthread_cond_broadcast(&finished);
    }
    pthread_mutex_unlock(&lock);
    return NULL;
}

void loader_start(void)
{
    if (running)
        return;
    running = 1;
    if (pthread_create(&worker, NULL, work, NULL) != 0)
        running = 0;
}

void loader_stop(void)
{
    if (!running)
        return;
    pthread_mutex_lock(&lock);
    running = 0;
    pthread_cond_broadcast(&wake);
    pthread_mutex_unlock(&lock);
    pthread_join(worker, NULL);
    for (int i = 0; i < LOADER_JOBS; i++) {
        if (jobs[i].state == JOB_DONE)
            UnloadImage(jobs[i].image);
        jobs[i].state = JOB_FREE;
    }
}

int loader_request(int32_t key, const char *path)
{
    if (!running)
        return 0;
    pthread_mutex_lock(&lock);
    int ok = 1;
    if (!find(key)) {
        Job *slot = NULL;
        for (int i = 0; i < LOADER_JOBS && !slot; i++)
            if (jobs[i].state == JOB_FREE)
                slot = &jobs[i];
        if (slot) {
            slot->state = JOB_QUEUED;
            slot->key = key;
            slot->order = next_order++;
            strncpy(slot->path, path, sizeof(slot->path) - 1);
            slot->path[sizeof(slot->path) - 1] = 0;
            memset(&slot->image, 0, sizeof(slot->image));
            pthread_cond_signal(&wake);
        } else {
            ok = 0;
        }
    }
    pthread_mutex_unlock(&lock);
    return ok;
}

int loader_pending(int32_t key)
{
    if (!running)
        return 0;
    pthread_mutex_lock(&lock);
    int pending = find(key) != NULL;
    pthread_mutex_unlock(&lock);
    return pending;
}

int loader_collect(int32_t *key, Image *image)
{
    if (!running)
        return 0;
    pthread_mutex_lock(&lock);
    Job *done = NULL;
    for (int i = 0; i < LOADER_JOBS && !done; i++)
        if (jobs[i].state == JOB_DONE)
            done = &jobs[i];
    if (done) {
        *key = done->key;
        *image = done->image;
        done->state = JOB_FREE;
    }
    pthread_mutex_unlock(&lock);
    return done != NULL;
}

int loader_wait(int32_t key, Image *image)
{
    memset(image, 0, sizeof(*image));
    if (!running)
        return 0;
    pthread_mutex_lock(&lock);
    Job *job = find(key);
    /* Still waiting its turn: take it off the queue and let the caller read
       it itself, rather than wait behind everything queued before it. */
    if (job && job->state == JOB_QUEUED) {
        job->state = JOB_FREE;
        job = NULL;
    }
    while (job && job->state == JOB_WORKING)
        pthread_cond_wait(&finished, &lock);
    int got = 0;
    if (job && job->state == JOB_DONE) {
        *image = job->image;
        job->state = JOB_FREE;
        got = image->data != NULL;
    }
    pthread_mutex_unlock(&lock);
    return got;
}
