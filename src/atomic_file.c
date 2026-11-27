// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Extracted from segagenesisrecomp; see PROVENANCE.md.
#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif
#include "recomp_persistence/atomic_file.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <io.h>
#include <fcntl.h>
#else
#include <unistd.h>
#endif

struct RecompAtomicFile {
    FILE *stream;
    char *target, *temporary;
    uint64_t limit, written;
    int failed, owns_temporary;
#ifdef _WIN32
    wchar_t *wide_target, *wide_temporary;
#endif
};

#ifdef _WIN32
static wchar_t *atomic_wide_path(const char *path)
{
    int n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,path,-1,NULL,0);
    if(!n)return NULL;
    wchar_t *w=malloc((size_t)n*sizeof *w);
    if(w&&!MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,path,-1,w,n)){free(w);return NULL;}
    return w;
}
#endif

static FILE *atomic_open_exclusive(RecompAtomicFile *file)
{
#ifdef _WIN32
    HANDLE handle=CreateFileW(file->wide_temporary,GENERIC_WRITE,0,NULL,CREATE_NEW,
                              FILE_ATTRIBUTE_NORMAL,NULL);
    if(handle==INVALID_HANDLE_VALUE){
        DWORD error=GetLastError();
        errno=(error==ERROR_FILE_EXISTS||error==ERROR_ALREADY_EXISTS)?EEXIST:EIO;
        return NULL;
    }
    file->owns_temporary=1;
    int fd=_open_osfhandle((intptr_t)handle,_O_WRONLY|_O_BINARY);
    if(fd<0){CloseHandle(handle);return NULL;}
    FILE *stream=_fdopen(fd,"wb");
    if(!stream)_close(fd);
    return stream;
#else
    FILE *stream=fopen(file->temporary,"wbx");
    if(stream)file->owns_temporary=1;
    return stream;
#endif
}

void recomp_atomic_file_abort(RecompAtomicFile *file)
{
    if(!file)return;
    if(file->stream)(void)fclose(file->stream);
    if(file->owns_temporary){
#ifdef _WIN32
        (void)_wremove(file->wide_temporary);
#else
        (void)remove(file->temporary);
#endif
    }
#ifdef _WIN32
    free(file->wide_target);free(file->wide_temporary);
#endif
    free(file->target);free(file->temporary);free(file);
}

RecompAtomicFile *recomp_atomic_file_begin(const char *path,uint64_t max_bytes)
{
    if(!path||!*path||strlen(path)>SIZE_MAX-64)return NULL;
    RecompAtomicFile *file=calloc(1,sizeof *file);
    if(!file)return NULL;
    size_t length=strlen(path)+1,capacity=length+63;
    file->target=malloc(length);file->temporary=malloc(capacity);file->limit=max_bytes;
    if(!file->target||!file->temporary)goto failed;
    memcpy(file->target,path,length);
#ifdef _WIN32
    file->wide_target=atomic_wide_path(path);
    if(!file->wide_target)goto failed;
    unsigned long pid=GetCurrentProcessId();
#else
    unsigned long pid=(unsigned long)getpid();
#endif
    /* Live allocations are distinct between concurrent transactions. Exclusive
     * creation also preserves stale/foreign files after address/PID reuse. */
    for(unsigned attempt=0;attempt<16;++attempt){
        (void)snprintf(file->temporary,capacity,"%s.tmp-%lu-%p-%u",path,pid,(void*)file,attempt);
#ifdef _WIN32
        free(file->wide_temporary);
        file->wide_temporary=atomic_wide_path(file->temporary);
        if(!file->wide_temporary)goto failed;
#endif
        file->stream=atomic_open_exclusive(file);
        if(file->stream)return file;
        if(file->owns_temporary||errno!=EEXIST)break;
    }
failed:
    recomp_atomic_file_abort(file);return NULL;
}

int recomp_atomic_file_write(RecompAtomicFile *file,const void *data,size_t bytes)
{
    if(!file)return 0;
    if(file->failed)return 0;
    if((!data&&bytes)||(uint64_t)bytes>file->limit-file->written){
        file->failed=1;return 0;
    }
    if(bytes&&fwrite(data,1,bytes,file->stream)!=bytes){file->failed=1;return 0;}
    file->written+=(uint64_t)bytes;return 1;
}

int recomp_atomic_file_flush(RecompAtomicFile *file)
{
    if(!file||file->failed)return 0;
    if(fflush(file->stream)||ferror(file->stream)){file->failed=1;return 0;}
    return 1;
}

int recomp_atomic_file_commit(RecompAtomicFile *file)
{
    if(!file)return 0;
    int ok=recomp_atomic_file_flush(file);
#ifdef _WIN32
    if(ok&&_commit(_fileno(file->stream)))ok=0;
#else
    if(ok&&fsync(fileno(file->stream)))ok=0;
#endif
    if(fclose(file->stream))ok=0;
    file->stream=NULL;
#ifdef _WIN32
    if(ok&&!MoveFileExW(file->wide_temporary,file->wide_target,
                        MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))ok=0;
#else
    if(ok&&rename(file->temporary,file->target))ok=0;
#endif
    if(ok)file->owns_temporary=0;
    recomp_atomic_file_abort(file);return ok;
}

int recomp_atomic_file_store(const char *path,const void *data,size_t bytes)
{
    if(!data&&bytes)return 0;
    RecompAtomicFile *file=recomp_atomic_file_begin(path,(uint64_t)bytes);
    if(!file)return 0;
    if(!recomp_atomic_file_write(file,data,bytes)){recomp_atomic_file_abort(file);return 0;}
    return recomp_atomic_file_commit(file);
}
