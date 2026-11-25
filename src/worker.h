#ifndef RECOMP_PERSISTENCE_WORKER_H
#define RECOMP_PERSISTENCE_WORKER_H
#ifdef __cplusplus
extern "C" {
#endif
typedef struct RecompWorker RecompWorker;
RecompWorker* recomp_worker_create(void);
void recomp_worker_start(RecompWorker*, void (*job)(void*), void*);
int recomp_worker_busy(RecompWorker*);
void recomp_worker_join(RecompWorker*);
void recomp_worker_destroy(RecompWorker*);
#ifdef __cplusplus
}
#endif
#endif
