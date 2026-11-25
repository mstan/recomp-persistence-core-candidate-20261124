// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Extracted from segagenesisrecomp; see PROVENANCE.md.
#ifndef RECOMP_HOST_ATOMIC_FILE_H
#define RECOMP_HOST_ATOMIC_FILE_H
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
/* UTF-8 path; exclusive same-directory temporary, commit then atomic replace.
 * Failure preserves the prior destination. Concurrent independent writers
 * have disjoint temporaries. NULL data is permitted only for zero bytes. */
int recomp_atomic_file_store(const char *, const void *, size_t);
#ifdef __cplusplus
}
#endif
#endif
