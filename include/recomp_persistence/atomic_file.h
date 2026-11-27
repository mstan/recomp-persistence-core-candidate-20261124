// SPDX-License-Identifier: MIT
// Extracted from segagenesisrecomp; see PROVENANCE.md.
#ifndef RECOMP_HOST_ATOMIC_FILE_H
#define RECOMP_HOST_ATOMIC_FILE_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct RecompAtomicFile RecompAtomicFile;
/* UTF-8 path on Windows, native path bytes on POSIX. Begin binds an absolute
 * target once and derives its exclusive same-directory temporary from it.
 * Relative paths remain valid through later process CWD changes. max_bytes is an
 * inclusive resource limit, including zero for an empty file. No worker,
 * whole-file buffer or serializer is introduced. Calls on one transaction
 * must be serialized; independent transactions may target the same file. */
RecompAtomicFile *recomp_atomic_file_begin(const char *path, uint64_t max_bytes);
/* Creation profile: publish only if the final name is still absent at commit.
 * Existing regular files, saves, directories and symlinks are never replaced or
 * opened for writing. Concurrent creators have exactly one successful winner.
 * POSIX staging is private (0600); atomic link publication must be supported by
 * the destination filesystem. No check-then-rename fallback is used. */
RecompAtomicFile *recomp_atomic_file_begin_create(const char *path, uint64_t max_bytes);
/* Append all bytes or mark the transaction failed. A failed/over-limit write
 * is sticky: commit can never publish it. NULL data is valid only for zero
 * bytes. The caller retains its input as soon as write returns. */
int recomp_atomic_file_write(RecompAtomicFile *, const void *, size_t);
/* Flush staging bytes; commit additionally synchronizes the file before
 * closing and publishing under the selected replacement/creation profile. */
int recomp_atomic_file_flush(RecompAtomicFile *);
/* Both operations consume the handle, including on failure. Commit returns
 * success only after publication. Abort, failed writes and failed publication
 * preserve the prior destination and remove only this transaction's temporary.
 * Concurrent commits publish complete files in their actual commit order.
 * Windows handle-based replacement accepts delete-sharing open targets where
 * supported; the older atomic-move fallback safely refuses unavailable cases.
 * A caller retaining a backing handle must reopen the committed destination. */
int recomp_atomic_file_commit(RecompAtomicFile *);
void recomp_atomic_file_abort(RecompAtomicFile *);
/* Buffer convenience API using the same transaction owner. */
int recomp_atomic_file_store(const char *, const void *, size_t);
int recomp_atomic_file_create(const char *, const void *, size_t);
/* Native exclusive creation, matching CREATE_NEW/O_EXCL on every writable
 * filesystem, including those without staging-link/rename support. Never
 * opens an existing target. Uses the same bounded write/flush/close owner,
 * but exposes the newly created name while writing and leaves an incomplete
 * new file on error. Select this only when that existing caller contract is
 * intended; success still requires all bytes, synchronization and close. */
int recomp_file_create_exclusive(const char *, const void *, size_t);
#ifdef __cplusplus
}
#endif
#endif
