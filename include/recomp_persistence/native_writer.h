// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Extracted from segagenesisrecomp; see PROVENANCE.md.
#ifndef RECOMP_PERSIST_WRITER_H
#define RECOMP_PERSIST_WRITER_H
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct RecompPersistWriter RecompPersistWriter;
typedef int (*RecompPersistWrite)(const char *, const void *, size_t);
typedef void (*RecompPersistCompleted)(const char *, const void *, size_t, void *);
/* The callback writes immutable fixed-size bytes and reports durable/atomic
 * completion. The caller retains its live state; path and bytes are copied. */
RecompPersistWriter *recomp_persist_create(const char *, const void *, size_t,
                                             RecompPersistWrite);
/* Nonblocking health check: 1 healthy (including busy), 0 failed. The internal
 * committed comparison advances only after a completed successful callback. */
int recomp_persist_poll(RecompPersistWriter *);
/* Configure on the producer before submission. Notifications run only on the
 * producer during poll/wait, after durable success, with committed snapshot
 * bytes borrowed for the duration of the callback. Never run on the worker. */
void recomp_persist_on_completed(RecompPersistWriter *, RecompPersistCompleted, void *);
/* 1 accepted/already committed, 0 prior snapshot still busy, -1 write failed.
 * Acceptance does not acknowledge persistence; flush waits for completion. */
int recomp_persist_submit(RecompPersistWriter *, const void *);
/* Preserve callers that require an actual replacement even for equal bytes. */
int recomp_persist_submit_force(RecompPersistWriter *, const void *);
/* Drain only the accepted snapshot; do not publish later mutable live data. */
int recomp_persist_wait(RecompPersistWriter *);
int recomp_persist_flush(RecompPersistWriter *, const void *latest);
/* Drains an accepted job; does not manufacture an unsubmitted snapshot. */
int recomp_persist_destroy(RecompPersistWriter *);
#ifdef __cplusplus
}
#endif
#endif
