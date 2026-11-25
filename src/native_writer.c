// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Extracted from segagenesisrecomp; see PROVENANCE.md.
#include "recomp_persistence/native_writer.h"
#include "worker.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

struct RecompPersistWriter {
    RecompWorker *worker;
    RecompPersistWrite write;
    RecompPersistCompleted completed;
    void *completed_context;
    char *path;
    size_t size;
    unsigned char *committed, *pending;
    int inflight, job_ok, failed, worker_attempted;
};
static void persist_job(void *context)
{
    RecompPersistWriter *p = context;
    p->job_ok = p->write(p->path, p->pending, p->size);
}
RecompPersistWriter *recomp_persist_create(const char *path, const void *initial,
                                             size_t size, RecompPersistWrite write)
{
    if (!path || !*path || !initial || !size || !write ||
        size > (SIZE_MAX-sizeof(RecompPersistWriter))/2) return NULL;
    RecompPersistWriter *p = calloc(1, sizeof *p + size*2);
    if (!p) return NULL;
    size_t length = strlen(path);
    if (length == SIZE_MAX) { free(p); return NULL; }
    p->path = malloc(length+1);
    if (!p->path) { free(p); return NULL; }
    memcpy(p->path, path, length+1);
    p->size = size; p->write = write;
    p->committed = (unsigned char *)(p+1);
    p->pending = p->committed + size;
    memcpy(p->committed, initial, size);
    return p;
}
int recomp_persist_poll(RecompPersistWriter *p)
{
    if (!p) return 0;
    if (p->inflight && !recomp_worker_busy(p->worker)) {
        recomp_worker_join(p->worker);
        p->inflight = 0;
        if (!p->job_ok) p->failed = 1;
        else {
            memcpy(p->committed, p->pending, p->size);
            if(p->completed)p->completed(p->path,p->committed,p->size,p->completed_context);
        }
    }
    return !p->failed;
}
void recomp_persist_on_completed(RecompPersistWriter *p, RecompPersistCompleted completed, void *context)
{
    if(p){p->completed=completed;p->completed_context=context;}
}
static int submit(RecompPersistWriter *p, const void *data, int force)
{
    if (!p || !data || !recomp_persist_poll(p)) return -1;
    if (p->inflight) return 0;
    if (!force && !memcmp(p->committed, data, p->size)) return 1;
    if (!p->worker_attempted) {
        p->worker_attempted = 1; p->worker = recomp_worker_create();
    }
    memcpy(p->pending, data, p->size);
    p->job_ok = 0; p->inflight = 1;
    recomp_worker_start(p->worker, persist_job, p);
    return recomp_persist_poll(p) ? 1 : -1;
}
int recomp_persist_submit(RecompPersistWriter *p, const void *data)
{return submit(p,data,0);}
int recomp_persist_submit_force(RecompPersistWriter *p, const void *data)
{return submit(p,data,1);}
int recomp_persist_wait(RecompPersistWriter *p)
{
    if(!p)return 0;
    recomp_worker_join(p->worker);return recomp_persist_poll(p);
}
int recomp_persist_flush(RecompPersistWriter *p, const void *latest)
{
    if (!p || !latest) return 0;
    recomp_worker_join(p->worker);
    if (!recomp_persist_poll(p) || recomp_persist_submit(p, latest) < 0) return 0;
    recomp_worker_join(p->worker);
    return recomp_persist_poll(p);
}
int recomp_persist_destroy(RecompPersistWriter *p)
{
    if (!p) return 1;
    recomp_worker_join(p->worker);
    int ok = recomp_persist_poll(p);
    recomp_worker_destroy(p->worker);
    free(p->path); free(p);
    return ok;
}
